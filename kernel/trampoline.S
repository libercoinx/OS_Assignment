    .section .text
    .align 12
    .globl trampoline
trampoline:
    .globl uservec
uservec:
    j uservec            /* 尚未实现真正的用户态陷阱处理 */

    .globl userret
userret:
    j userret            /* 占位实现，后续填充实际返回逻辑 */

