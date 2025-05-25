idf_component_register(SRCS "monitoringnode.c" 
                            "main.h" 
                            "sdcard.c" 
                            "sdcard.h" 
                            "pt928.c" 
                            "sensors.h" 
                            "sensors.c" 
                            "upload.c" 
                            "upload.h" 
                            "time.c"
                            "html.c"
                            "html.h"
                            "wifi.c"
                            "wifi.h"
                            "LED.c"
                            "LED.h"
                    INCLUDE_DIRS ".")

target_add_binary_data(${COMPONENT_TARGET} "DigiCertGlobalRootG2.crt.pem" TEXT)

