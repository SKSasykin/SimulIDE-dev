if(IDF_VERSION_MAJOR GREATER_EQUAL 5)
    set(GPIO_COMPONENT esp_driver_gpio)
else()
    set(GPIO_COMPONENT driver)
endif()

idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES bt ${GPIO_COMPONENT} nvs_flash common)
