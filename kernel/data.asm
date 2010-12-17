; --------------------- data for ramdisk ---------------------------------------
global file_data_start
global file_data_end
file_data_start:
    incbin "initrd.dat"
file_data_end:
; --------------------- data for ramdisk ---------------------------------------

; --------------------- VM86 ---------------------------------------------------
global vidswtch_com_start
global vidswtch_com_end
global apm_com_start
global apm_com_end
vidswtch_com_start:
    incbin "user/vm86/VIDSWTCH.COM"
vidswtch_com_end:
apm_com_start:
    incbin "user/vm86/APM.COM"
apm_com_end:
; --------------------- VM86 ---------------------------------------------------

;---------------------- bmp ----------------------------------------------------
global bmp_start
global bmp_end
bmp_start:
    incbin "user/vm86/bootscr.bmp"
bmp_end:
;---------------------- bmp ----------------------------------------------------

;---------------------- bmp cursor ---------------------------------------------
global cursor_start
global cursor_end
cursor_start:
    incbin "user/vm86/cursor.bmp"
cursor_end:
;---------------------- bmp cursor ---------------------------------------------