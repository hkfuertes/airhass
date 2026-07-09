@echo set autotools mode
if exist jansson\src\jansson_config.h.autotools (
	ren jansson\src\jansson_config.h.autotools jansson_config.h
)	