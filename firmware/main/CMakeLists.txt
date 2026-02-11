idf_component_register(
    SRCS
        "main.c"

        "../drivers/lcd_st7701.c"
        "../drivers/touch_gt911.c"

        "../services/mqtt_service.c"
        "../services/wifi_service.c"
        "../services/time_service.c"

        "../ui/ui.c"
        "../ui/qr_screen.c"
        "../ui/bg_rain.c"  
		"../ui/font_vietnam_20.c"
    INCLUDE_DIRS
        "."
        "../drivers"
        "../services"
        "../ui"
        "../secrets"
)