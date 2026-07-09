@echo set cmake mode
if exist jansson\src\jansson_config.h (
	ren jansson\src\jansson_config.h jansson_config.h.autotools
)	
