################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
"../VL53L0X_1.0.4/Api/platform/src/vl53l0x_platform.c" 

COMPILED_SRCS += \
"VL53L0X_1.0.4/Api/platform/src/vl53l0x_platform.src" 

C_DEPS += \
"./VL53L0X_1.0.4/Api/platform/src/vl53l0x_platform.d" 

OBJS += \
"VL53L0X_1.0.4/Api/platform/src/vl53l0x_platform.o" 


# Each subdirectory must supply rules for building sources it contributes
"VL53L0X_1.0.4/Api/platform/src/vl53l0x_platform.src":"../VL53L0X_1.0.4/Api/platform/src/vl53l0x_platform.c" "VL53L0X_1.0.4/Api/platform/src/subdir.mk"
	cctc -cs --dep-file="$*.d" --misrac-version=2012 -D__CPU__=tc27xd "-fC:/Vehicle_Anti-rollaway_Protection_System/Vehicle_Anti-rollaway_Protection_System/tof_test/TriCore Debug (TASKING)/TASKING_C_C___Compiler-Include_paths__-I_.opt" --iso=99 --c++14 --language=+volatile --exceptions --anachronisms --fp-model=3 -O0 --tradeoff=4 --compact-max-size=200 -g -Wc-w544 -Wc-w557 -Ctc27xd -Y0 -N0 -Z0 -o "$@" "$<"
"VL53L0X_1.0.4/Api/platform/src/vl53l0x_platform.o":"VL53L0X_1.0.4/Api/platform/src/vl53l0x_platform.src" "VL53L0X_1.0.4/Api/platform/src/subdir.mk"
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"

clean: clean-VL53L0X_1-2e-0-2e-4-2f-Api-2f-platform-2f-src

clean-VL53L0X_1-2e-0-2e-4-2f-Api-2f-platform-2f-src:
	-$(RM) ./VL53L0X_1.0.4/Api/platform/src/vl53l0x_platform.d ./VL53L0X_1.0.4/Api/platform/src/vl53l0x_platform.o ./VL53L0X_1.0.4/Api/platform/src/vl53l0x_platform.src

.PHONY: clean-VL53L0X_1-2e-0-2e-4-2f-Api-2f-platform-2f-src

