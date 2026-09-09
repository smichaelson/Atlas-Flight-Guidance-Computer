; Factory-ROM trampoline. R0 = new MSP; R1 = Thumb reset-handler address.
; Do not access C locals or return after changing MSP.
        MODULE atlas_boot_branch
        SECTION .text:CODE:NOROOT(2)
        THUMB
        PUBLIC AtlasBoot_Branch
AtlasBoot_Branch
        MSR MSP, R0
        CPSIE I
        BX R1
        END
