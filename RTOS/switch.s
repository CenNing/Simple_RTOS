    PRESERVE8                       ; 保持 8 字节栈对齐（C 调用规范要求）
    AREA    SWITCH, CODE, READONLY   ; 定义一个名为 SWITCH 的只读代码段
    EXPORT  task_start               ; 导出 task_start  供 C 文件调用
    EXPORT  PendSV_Handler           ; 导出 PendSV_Handler 作为中断向量表中的 ISR

    IMPORT  current_task             ; 导入 C 全局变量：当前正在运行的任务指针（cos.c）
    IMPORT  next_task                ; 导入 C 全局变量：下一个要运行的任务指针（cos.c）
    IMPORT  scheduler                ; 导入 C 函数：轮转调度器，选择 next_task（cos.c）

;------------------PendSV_Handler: SysTick 触发 PendSV 自动切换------------------
; PendSV 中断入口：SysTick 通过设置 PendSV 标记触发
; 流程：保存当前任务 → 调用 scheduler 选出下一个 → 恢复下一个任务
; 关键：恢复时加载 EXC_RETURN，硬件自动从 PSP 栈弹出伪造帧并跳转到 PC

PendSV_Handler
;-------------保存当前任务上下文---------------------------
    ; 保存当前任务（从全局指针 current_task 获取结构体地址）
    push    {r0}
    ldr     r0, =current_task
    ldr     r0, [r0]                ; r0 = current TASK_ADDR*

    ; 先保存易被破坏的寄存器（push 占用了 r0，先将 r1-r3 保存，再处理原始 r0）
    str     r1, [r0, #4]            ; 保存 r1
    str     r2, [r0, #8]            ; 保存 r2
    str     r3, [r0, #12]           ; 保存 r3

    ; 读取特殊寄存器
    mrs     r1, xPSR                ; 读 xPSR
    str     r1, [r0, #52]           ; 保存 xPSR

    ; 取回原始 r0 再保存
    mov     r2, r0                  ; r2 = 结构体指针
    pop     {r1}
    str     r1, [r2, #0]            ; 保存原始 r0

    ; 保存其余寄存器（r4-r12, lr, sp）
    str     r4, [r2, #16]
    str     r5, [r2, #20]
    str     r6, [r2, #24]
    str     r7, [r2, #28]
    str     r8, [r2, #32]
    str     r9, [r2, #36]
    str     r10, [r2, #40]
    str     r11, [r2, #44]
    str     r12, [r2, #48]
    str     lr, [r2, #56]           ; 保存 EXC_RETURN
    mrs     r3, PSP                 ; 读取 PSP（任务实际使用的栈指针）
    str     r3, [r2, #60]           ; 保存 PSP 到 stack_addr


;----------调用调度器选择下一个任务----------------------------
    
    push    {r0-r3, lr}             ; 保存调用者寄存器
    bl      scheduler               ; 调用 scheduler，更新 next_task
    pop     {r0-r3, lr}             ; 恢复

;----------恢复下一个任务的上下文--------------------------------
    ; 恢复下一个任务的上下文
    ldr     r1, =next_task
    ldr     r1, [r1]                ; r1 = next TASK_ADDR*

    ; 更新 current_task
    ldr     r2, =current_task
    str     r1, [r2]

    ; 恢复 R4-R11（伪造帧中的 R0-R3,R12,LR,PC,xPSR 会被异常返回硬件自动恢复）
    ldr     r11, [r1, #44]
    ldr     r10, [r1, #40]
    ldr     r9, [r1, #36]
    ldr     r8, [r1, #32]
    ldr     r7, [r1, #28]
    ldr     r6, [r1, #24]
    ldr     r5, [r1, #20]
    ldr     r4, [r1, #16]

    ; 恢复下一个任务的 PSP
    ldr     r0, [r1, #60]           ; r0 = stack_addr（下一个任务的 PSP）
    msr     PSP, r0                 ; 设置 PSP

    ; 使用 EXC_RETURN 触发异常返回
    ldr     lr, [r1, #56]           ; 从 [r1, #56] = 结构体 exc_return 加载 EXC_RETURN 魔法值
    bx      lr                      ; 跳转 EXC_RETURN：硬件自动从 PSP 栈弹出 R0-R3,R12,LR,PC,xPSR，然后跳转到任务




;------------------task_start: 启动第一个任务-----------------
; 参数: r0 = to_addr
; 注意：设置 Thread 模式，手动从 PSP 弹出伪造异常帧
; 任务栈已预填伪造异常帧，通过 bx r12 跳转到入口（不使用 EXC_RETURN）

task_start
    mov     r12, r0                ; r0为task_start()参数，即当前任务TCB指针，保存当前指针到 r12

    ; 设置 PSP 为任务栈指针，进入 Thread 模式使用 PSP
    ldr     r0, [r12, #60]         ; r0 = stack_addr  将TCB内保存的任务栈地址存入r0
    msr     PSP, r0                ; 将任务栈指针存入PSP
    mov     r0, #2                 
    msr     CONTROL, r0            ; 将CONTROL寄存器的SPSEL = 1: 开启Thread 模式使用 PSP
    isb                            ; 指令同步屏障，此后 SP 即 PSP

    ; 从r12保存的TCB结构体恢复 R4-R11
    ldr     r11, [r12, #44]        ; r11
    ldr     r10, [r12, #40]        ; r10
    ldr     r9, [r12, #36]         ; r9
    ldr     r8, [r12, #32]         ; r8
    ldr     r7, [r12, #28]         ; r7
    ldr     r6, [r12, #24]         ; r6
    ldr     r5, [r12, #20]         ; r5
    ldr     r4, [r12, #16]         ; r4

    ; 手动从 PSP 栈弹出异常帧
    ; 异常帧内容（PSP 低地址→高地址）: R0, R1, R2, R3, R12, LR, PC, xPSR
    pop     {r0-r3}                ; 恢复 R0-R3
    add     sp, sp, #4             ; 跳过 R12（初始值不重要）
    pop     {lr}                   ; LR = task_exit 死循环函数，如果第一个任务不是循环执行，则会跳转到这个函数，定位任务非循环bug
    pop     {r12}                  ; PC = task_entry第一个任务地址
    add     sp, sp, #4             ; 跳过 xPSR（初始值 0x01000000，不需要恢复）
    bx      r12                    ; 跳转到任务入口，bx表示不需要返回，bl会操作LR，表示需要返回

END





















