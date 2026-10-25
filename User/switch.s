    PRESERVE8                       ; 保持 8 字节栈对齐（C 调用规范要求）
    AREA    SWITCH, CODE, READONLY   ; 定义一个名为 SWITCH 的只读代码段
    EXPORT  task_switch              ; 导出 task_switch 供 C 文件调用
    EXPORT  task_start               ; 导出 task_start  供 C 文件调用
    EXPORT  PendSV_Handler           ; 导出 PendSV_Handler 作为中断向量表中的 ISR

    IMPORT  current_task             ; 导入 C 全局变量：当前正在运行的任务指针
    IMPORT  next_task                ; 导入 C 全局变量：下一个要运行的任务指针
    IMPORT  scheduler                ; 导入 C 函数：轮转调度器，选择 next_task

;------------------PendSV_Handler: SysTick 触发 PendSV 自动切换------------------
; PendSV 中断入口：SysTick 通过设置 PendSV 标记触发
; 流程：保存当前任务 → 调用 scheduler 选出下一个 → 恢复下一个任务
; 关键：恢复时加载 EXC_RETURN，硬件自动从 PSP 栈弹出伪造帧并跳转到 PC

PendSV_Handler
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

    ; 调用调度器选择下一个任务
    push    {r0-r3, lr}             ; 保存调用者寄存器（也可不保存，因为值已存入结构体，可直接再读取）
    bl      scheduler               ; 调用 scheduler，更新 next_task
    pop     {r0-r3, lr}             ; 恢复

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


;------------------task_switch: 手动切换任务（供用户/API 调用）--------------
; 参数: r0 = from_addr, r1 = to_addr
; 结构体偏移: r0(0) r1(4) r2(8) ... r12(48) apsr(52) exc_return(56) stack_addr(60)

task_switch
    ;----------保存当前任务上下文----------
    str     r0, [r0, #0]           ; 存 r0
    str     r1, [r0, #4]           ; 存 r1
    str     r2, [r0, #8]           ; 存 r2
    str     r3, [r0, #12]          ; 存 r3
    str     r4, [r0, #16]          ; 存 r4
    str     r5, [r0, #20]          ; 存 r5
    str     r6, [r0, #24]          ; 存 r6
    str     r7, [r0, #28]          ; 存 r7
    str     r8, [r0, #32]          ; 存 r8
    str     r9, [r0, #36]          ; 存 r9
    str     r10, [r0, #40]         ; 存 r10
    str     r11, [r0, #44]         ; 存 r11
    str     r12, [r0, #48]         ; 存 r12

    mrs     r2, xPSR               ; 读 xPSR
    str     r2, [r0, #52]          ; 存 apsr

    str     lr, [r0, #56]          ; 保存 lr 到 exc_return (值为 EXC_RETURN)
    str     sp, [r0, #60]          ; 存 stack_addr

    ;----------恢复目标任务上下文----------
    ldr     sp, [r1, #60]          ; 恢复 SP
    ldr     lr, [r1, #56]          ; 恢复 LR

    ldr     r2, [r1, #52]          ; 恢复 xPSR
    msr     xPSR, r2

    ldr     r0, [r1, #0]           ; 恢复 r0
    ldr     r2, [r1, #8]           ; 恢复 r2
    ldr     r3, [r1, #12]          ; 恢复 r3
    ldr     r4, [r1, #16]          ; 恢复 r4
    ldr     r5, [r1, #20]          ; 恢复 r5
    ldr     r6, [r1, #24]          ; 恢复 r6
    ldr     r7, [r1, #28]          ; 恢复 r7
    ldr     r8, [r1, #32]          ; 恢复 r8
    ldr     r9, [r1, #36]          ; 恢复 r9
    ldr     r10, [r1, #40]         ; 恢复 r10
    ldr     r11, [r1, #44]         ; 恢复 r11
    ldr     r12, [r1, #48]         ; 恢复 r12
    ldr     r1, [r1, #4]           ; 恢复 r1（最后恢复，结构体指针不丢失）
    bx      lr                     ;


;------------------task_start: 启动第一个任务-----------------
; 参数: r0 = to_addr
; 注意：设置 Thread 模式，手动从 PSP 弹出伪造异常帧
; 任务栈已预填伪造异常帧，通过 bx r12 跳转到入口（不使用 EXC_RETURN）

task_start
    mov     r12, r0                ; 保存结构体指针到 r12

    ; 设置 PSP 为任务栈指针，进入 Thread 模式使用 PSP
    ldr     r0, [r12, #60]         ; r0 = stack_addr（指向伪造异常帧起始 R0）
    msr     PSP, r0                ; PSP = 任务栈指针
    mov     r0, #2                 ; SPSEL = 1: Thread 模式使用 PSP
    msr     CONTROL, r0
    isb                            ; 指令同步屏障，此后 SP 即 PSP

    ; 从结构体恢复 R4-R11（callee-saved，任务使用前可被 push 保存）
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
    pop     {lr}                   ; LR = task_exit（任务如果返回则进入退出处理）
    pop     {r12}                  ; PC = task_entry（Thumb 模式，LSB=1）
    add     sp, sp, #4             ; 跳过 xPSR（初始值 0x01000000，不需要恢复）
    bx      r12                    ; 跳转到任务入口

    END
