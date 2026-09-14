/**
 * @file lv_conf.h
 * LVGL v8.3 配置 — STM32F103ZET6 精英版 + ILI9341 2.8" LCD
 * Smart Factory v2.2 (纯 LVGL, 无 FreeRTOS)
 *
 * 内存预算 (64KB SRAM):
 *   LVGL draw buf (双缓冲 static) . 320*20*2*2 = 25.6 KB (见 config.h)
 *   LVGL 对象池 (LV_MEM_SIZE) .... 24 KB (本文件)
 *   ESP8266 ring buffer ........... 2 KB
 *   其他全局/static 变量 .......... ~3 KB
 *   主栈 MSP (启动文件) ........... 4 KB
 *   合计 ≈ 59 KB, 剩余 ~5 KB 余量
 *
 * v2.2 相对 v2.0 的关键变化:
 *   - LV_MEM_CUSTOM=0: 不再借用 FreeRTOS 的 pvPortMalloc,
 *     改用 LVGL 内置对象池 (确定性分配, 不依赖启动文件 Heap_Size)
 *   - RAM 不够时先减 LV_MEM_SIZE (24→20KB), 再减 config.h 的
 *     LVGL_DISP_BUF_LINES (20→15, 省 6.4KB)
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>
#include <stddef.h>

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH          16      /* RGB565, 与 ILI9341 一致 */
#define LV_COLOR_16_SWAP        0       /* FSMC 16-bit 总线不需要交换 */
#define LV_COLOR_SCREEN_TRANSP  0
#define LV_COLOR_MIX_ROUND_OFS  0
#define LV_COLOR_CHROMA_KEY     lv_color_hex(0x00ff00)

/*=========================
   MEMORY SETTINGS
 *=========================*/
/* v2.2: 使用 LVGL 内置内存池 (lv_mem), 不依赖 FreeRTOS / C 库堆 */
#define LV_MEM_CUSTOM           0
#if LV_MEM_CUSTOM
  /* (未启用) 如需改回外部分配, 打开 LV_MEM_CUSTOM=1 并配置: */
  #define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
  #define LV_MEM_CUSTOM_ALLOC   malloc
  #define LV_MEM_CUSTOM_FREE    free
  #define LV_MEM_CUSTOM_REALLOC realloc
#endif

#define LV_MEM_SIZE             (24U * 1024U)   /* LVGL 对象/样式/动画 池 */
#define LV_MEM_ADR              0
#define LV_MEM_BUF_MAX_NUM      16
#define LV_MEMCPY_MEMSET_STD    1

/*====================
   HAL SETTINGS
 *====================*/
/* LVGL 直接读 sys_tick() 作为时间源 (bsp.c 的 SysTick 毫秒计数),
 * 不需要手动调 lv_tick_inc, 也没有 FreeRTOS tick hook */
#define LV_TICK_CUSTOM          1
#if LV_TICK_CUSTOM
  #define LV_TICK_CUSTOM_INCLUDE "bsp.h"
  #define LV_TICK_CUSTOM_SYS_TIME_EXPR (sys_tick())
#endif

/*====================
   FEATURE CONFIGURATION
 *====================*/
#define LV_DRAW_COMPLEX         1       /* 需要圆角/渐变/阴影 */
#if LV_DRAW_COMPLEX
  #define LV_SHADOW_CACHE_SIZE  0
  #define LV_CIRCLE_CACHE_SIZE  4
#endif

#define LV_USE_FONT_PLACEHOLDER 1
#define LV_USE_ANIMIMG          0
#define LV_USE_MEM_MONITOR      0
#define LV_USE_PERF_MONITOR     0
#define LV_USE_USER_DATA        1
#define LV_USE_REFR_DEBUG       0

/*====================
   FONT USAGE
 *====================*/
#define LV_FONT_MONTSERRAT_8    0
#define LV_FONT_MONTSERRAT_10   0
#define LV_FONT_MONTSERRAT_12   0
#define LV_FONT_MONTSERRAT_14   1       /* 默认字体 */
#define LV_FONT_MONTSERRAT_16   1       /* 标题 */
#define LV_FONT_MONTSERRAT_18   0
#define LV_FONT_MONTSERRAT_20   1       /* 数值显示 */
#define LV_FONT_MONTSERRAT_22   0
#define LV_FONT_MONTSERRAT_24   0
#define LV_FONT_MONTSERRAT_26   0
#define LV_FONT_MONTSERRAT_28   1       /* 温度大字 */
#define LV_FONT_MONTSERRAT_30   0
#define LV_FONT_MONTSERRAT_32   0
#define LV_FONT_MONTSERRAT_34   0
#define LV_FONT_MONTSERRAT_36   0
#define LV_FONT_MONTSERRAT_38   0
#define LV_FONT_MONTSERRAT_40   0
#define LV_FONT_MONTSERRAT_42   0
#define LV_FONT_MONTSERRAT_44   0
#define LV_FONT_MONTSERRAT_46   0
#define LV_FONT_MONTSERRAT_48   0

#define LV_FONT_MONTSERRAT_12_SUBPX      0
#define LV_FONT_MONTSERRAT_28_COMPRESSED 0
#define LV_FONT_DEJAVU_16_PERSIAN_HEBREW 0
#define LV_FONT_SIMSUN_16_CJK            0
#define LV_FONT_UNSCII_8                 0
#define LV_FONT_UNSCII_16                0

#define LV_FONT_DEFAULT         &lv_font_montserrat_14
#define LV_USE_FONT_SUBPX       0

/*====================
   WIDGET USAGE
 *====================*/
#define LV_USE_ARC              0
#define LV_USE_BAR              1       /* 进度条 */
#define LV_USE_BTN              1       /* 按钮 */
#define LV_USE_BTNMATRIX        0
#define LV_USE_CANVAS           0
#define LV_USE_CHECKBOX         0
#define LV_USE_DROPDOWN         0
#define LV_USE_IMG              0
#define LV_USE_LABEL            1       /* 文本标签 */
#define LV_USE_LINE             1       /* 分隔线 */
#define LV_USE_ROLLER           0
#define LV_USE_SLIDER           0
#define LV_USE_SWITCH           0
#define LV_USE_TEXTAREA         0
#define LV_USE_TABLE            0

/*====================
   EXTRA COMPONENTS
 *====================*/
#define LV_USE_CALENDAR         0
#define LV_USE_CHART            0
#define LV_USE_COLORWHEEL       0
#define LV_USE_IMGBTN           0
#define LV_USE_KEYBOARD         0
#define LV_USE_LED              0
#define LV_USE_LIST             0
#define LV_USE_MENU             0
#define LV_USE_METER            0
#define LV_USE_MSGBOX           0
#define LV_USE_SPAN             0
#define LV_USE_SPINBOX          0
#define LV_USE_SPINNER          0
#define LV_USE_TABVIEW          0
#define LV_USE_TILEVIEW         0
#define LV_USE_WIN              0

/*====================
   THEME USAGE
 *====================*/
#define LV_USE_THEME_DEFAULT    1
#if LV_USE_THEME_DEFAULT
  #define LV_THEME_DEFAULT_DARK            0
  #define LV_THEME_DEFAULT_GROW            0
  #define LV_THEME_DEFAULT_TRANSITION_TIME 0
#endif

#define LV_USE_THEME_BASIC      1
#define LV_USE_THEME_MONO       0

/*====================
   LAYOUT USAGE
 *====================*/
#define LV_USE_FLEX             1       /* 控制按钮区用了 flex 布局 */
#define LV_USE_GRID             0

/*====================
   FILE SYSTEM / IMAGE DECODE / OTHERS
 *====================*/
#define LV_USE_FS_STDIO         0
#define LV_USE_FS_POSIX         0
#define LV_USE_FS_WIN32         0
#define LV_USE_FS_FATFS         0
#define LV_USE_FS_LITTLEFS      0

#define LV_USE_PNG              0
#define LV_USE_GIF              0
#define LV_USE_BMP              0
#define LV_USE_SJPG             0
#define LV_USE_QRCODE           0

#define LV_BUILD_EXAMPLES       0

#endif /* LV_CONF_H */
