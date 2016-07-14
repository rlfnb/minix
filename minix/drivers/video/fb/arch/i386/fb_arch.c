/* Architecture dependent part for the framebuffer on i386.
 * There's obvious room for improvement.
 */

#include <minix/chardriver.h>
#include <minix/drivers.h>
#include <minix/fb.h>
#include <minix/sysutil.h>
#include <minix/type.h>
#include <minix/vm.h>
#include <minix/log.h>
#include <assert.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <dev/videomode/videomode.h>
#include <dev/videomode/edidvar.h>
#include <dev/videomode/edidreg.h>
#include "fb.h"

/* logging - use with log_warn(), log_info(), log_debug(), log_trace() */
static struct log log = {
	.name = "fb",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

int arch_fb_init(int minor, struct edid_info *info){
	return 0;
}
int arch_get_device(int minor, struct device *dev){
        return 0;
}
int arch_get_varscreeninfo(int minor, struct fb_var_screeninfo *fbvsp){
        return 0;
}
int arch_put_varscreeninfo(int minor, struct fb_var_screeninfo *fbvs_copy){
        return 0;
}
int arch_get_fixscreeninfo(int minor, struct fb_fix_screeninfo *fbfsp){
        return 0;
}
int arch_pan_display(int minor, struct fb_var_screeninfo *fbvs_copy){
        return 0;
}

