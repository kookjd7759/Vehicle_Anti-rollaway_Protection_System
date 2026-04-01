################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
"../VL53L0X_1.0.4/Api/core/src/vl53l0x_api.c" \
"../VL53L0X_1.0.4/Api/core/src/vl53l0x_api_calibration.c" \
"../VL53L0X_1.0.4/Api/core/src/vl53l0x_api_core.c" \
"../VL53L0X_1.0.4/Api/core/src/vl53l0x_api_ranging.c" \
"../VL53L0X_1.0.4/Api/core/src/vl53l0x_api_strings.c" 

COMPILED_SRCS += \
"VL53L0X_1.0.4/Api/core/src/vl53l0x_api.src" \
"VL53L0X_1.0.4/Api/core/src/vl53l0x_api_calibration.src" \
"VL53L0X_1.0.4/Api/core/src/vl53l0x_api_core.src" \
"VL53L0X_1.0.4/Api/core/src/vl53l0x_api_ranging.src" \
"VL53L0X_1.0.4/Api/core/src/vl53l0x_api_strings.src" 

C_DEPS += \
"./VL53L0X_1.0.4/Api/core/src/vl53l0x_api.d" \
"./VL53L0X_1.0.4/Api/core/src/vl53l0x_api_calibration.d" \
"./VL53L0X_1.0.4/Api/core/src/vl53l0x_api_core.d" \
"./VL53L0X_1.0.4/Api/core/src/vl53l0x_api_ranging.d" \
"./VL53L0X_1.0.4/Api/core/src/vl53l0x_api_strings.d" 

OBJS += \
"VL53L0X_1.0.4/Api/core/src/vl53l0x_api.o" \
"VL53L0X_1.0.4/Api/core/src/vl53l0x_api_calibration.o" \
"VL53L0X_1.0.4/Api/core/src/vl53l0x_api_core.o" \
"VL53L0X_1.0.4/Api/core/src/vl53l0x_api_ranging.o" \
"VL53L0X_1.0.4/Api/core/src/vl53l0x_api_strings.o" 


# Each subdirectory must supply rules for building sources it contributes
"VL53L0X_1.0.4/Api/core/src/vl53l0x_api.src":"../VL53L0X_1.0.4/Api/core/src/vl53l0x_api.c" "VL53L0X_1.0.4/Api/core/src/subdir.mk"
	cctc -cs --dep-file="$*.d" --misrac-version=2012 -D__CPU__=tc27xd "-fC:/Vehicle_Anti-rollaway_Protection_System/Vehicle_Anti-rollaway_Protection_System/tof_test/TriCore Debug (TASKING)/TASKING_C_C___Compiler-Include_paths__-I_.opt" --iso=99 --c++14 --language=+volatile --exceptions --anachronisms --fp-model=3 -O0 --tradeoff=4 --compact-max-size=200 -g -Wc-w544 -Wc-w557 -Ctc27xd -Y0 -N0 -Z0 -o "$@" "$<"
"VL53L0X_1.0.4/Api/core/src/vl53l0x_api.o":"VL53L0X_1.0.4/Api/core/src/vl53l0x_api.src" "VL53L0X_1.0.4/Api/core/src/subdir.mk"
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
"VL53L0X_1.0.4/Api/core/src/vl53l0x_api_calibration.src":"../VL53L0X_1.0.4/Api/core/src/vl53l0x_api_calibration.c" "VL53L0X_1.0.4/Api/core/src/subdir.mk"
	cctc -cs --dep-file="$*.d" --misrac-version=2012 -D__CPU__=tc27xd "-fC:/Vehicle_Anti-rollaway_Protection_System/Vehicle_Anti-rollaway_Protection_System/tof_test/TriCore Debug (TASKING)/TASKING_C_C___Compiler-Include_paths__-I_.opt" --iso=99 --c++14 --language=+volatile --exceptions --anachronisms --fp-model=3 -O0 --tradeoff=4 --compact-max-size=200 -g -Wc-w544 -Wc-w557 -Ctc27xd -Y0 -N0 -Z0 -o "$@" "$<"
"VL53L0X_1.0.4/Api/core/src/vl53l0x_api_calibration.o":"VL53L0X_1.0.4/Api/core/src/vl53l0x_api_calibration.src" "VL53L0X_1.0.4/Api/core/src/subdir.mk"
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
"VL53L0X_1.0.4/Api/core/src/vl53l0x_api_core.src":"../VL53L0X_1.0.4/Api/core/src/vl53l0x_api_core.c" "VL53L0X_1.0.4/Api/core/src/subdir.mk"
	cctc -cs --dep-file="$*.d" --misrac-version=2012 -D__CPU__=tc27xd "-fC:/Vehicle_Anti-rollaway_Protection_System/Vehicle_Anti-rollaway_Protection_System/tof_test/TriCore Debug (TASKING)/TASKING_C_C___Compiler-Include_paths__-I_.opt" --iso=99 --c++14 --language=+volatile --exceptions --anachronisms --fp-model=3 -O0 --tradeoff=4 --compact-max-size=200 -g -Wc-w544 -Wc-w557 -Ctc27xd -Y0 -N0 -Z0 -o "$@" "$<"
"VL53L0X_1.0.4/Api/core/src/vl53l0x_api_core.o":"VL53L0X_1.0.4/Api/core/src/vl53l0x_api_core.src" "VL53L0X_1.0.4/Api/core/src/subdir.mk"
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
"VL53L0X_1.0.4/Api/core/src/vl53l0x_api_ranging.src":"../VL53L0X_1.0.4/Api/core/src/vl53l0x_api_ranging.c" "VL53L0X_1.0.4/Api/core/src/subdir.mk"
	cctc -cs --dep-file="$*.d" --misrac-version=2012 -D__CPU__=tc27xd "-fC:/Vehicle_Anti-rollaway_Protection_System/Vehicle_Anti-rollaway_Protection_System/tof_test/TriCore Debug (TASKING)/TASKING_C_C___Compiler-Include_paths__-I_.opt" --iso=99 --c++14 --language=+volatile --exceptions --anachronisms --fp-model=3 -O0 --tradeoff=4 --compact-max-size=200 -g -Wc-w544 -Wc-w557 -Ctc27xd -Y0 -N0 -Z0 -o "$@" "$<"
"VL53L0X_1.0.4/Api/core/src/vl53l0x_api_ranging.o":"VL53L0X_1.0.4/Api/core/src/vl53l0x_api_ranging.src" "VL53L0X_1.0.4/Api/core/src/subdir.mk"
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
"VL53L0X_1.0.4/Api/core/src/vl53l0x_api_strings.src":"../VL53L0X_1.0.4/Api/core/src/vl53l0x_api_strings.c" "VL53L0X_1.0.4/Api/core/src/subdir.mk"
	cctc -cs --dep-file="$*.d" --misrac-version=2012 -D__CPU__=tc27xd "-fC:/Vehicle_Anti-rollaway_Protection_System/Vehicle_Anti-rollaway_Protection_System/tof_test/TriCore Debug (TASKING)/TASKING_C_C___Compiler-Include_paths__-I_.opt" --iso=99 --c++14 --language=+volatile --exceptions --anachronisms --fp-model=3 -O0 --tradeoff=4 --compact-max-size=200 -g -Wc-w544 -Wc-w557 -Ctc27xd -Y0 -N0 -Z0 -o "$@" "$<"
"VL53L0X_1.0.4/Api/core/src/vl53l0x_api_strings.o":"VL53L0X_1.0.4/Api/core/src/vl53l0x_api_strings.src" "VL53L0X_1.0.4/Api/core/src/subdir.mk"
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"

clean: clean-VL53L0X_1-2e-0-2e-4-2f-Api-2f-core-2f-src

clean-VL53L0X_1-2e-0-2e-4-2f-Api-2f-core-2f-src:
	-$(RM) ./VL53L0X_1.0.4/Api/core/src/vl53l0x_api.d ./VL53L0X_1.0.4/Api/core/src/vl53l0x_api.o ./VL53L0X_1.0.4/Api/core/src/vl53l0x_api.src ./VL53L0X_1.0.4/Api/core/src/vl53l0x_api_calibration.d ./VL53L0X_1.0.4/Api/core/src/vl53l0x_api_calibration.o ./VL53L0X_1.0.4/Api/core/src/vl53l0x_api_calibration.src ./VL53L0X_1.0.4/Api/core/src/vl53l0x_api_core.d ./VL53L0X_1.0.4/Api/core/src/vl53l0x_api_core.o ./VL53L0X_1.0.4/Api/core/src/vl53l0x_api_core.src ./VL53L0X_1.0.4/Api/core/src/vl53l0x_api_ranging.d ./VL53L0X_1.0.4/Api/core/src/vl53l0x_api_ranging.o ./VL53L0X_1.0.4/Api/core/src/vl53l0x_api_ranging.src ./VL53L0X_1.0.4/Api/core/src/vl53l0x_api_strings.d ./VL53L0X_1.0.4/Api/core/src/vl53l0x_api_strings.o ./VL53L0X_1.0.4/Api/core/src/vl53l0x_api_strings.src

.PHONY: clean-VL53L0X_1-2e-0-2e-4-2f-Api-2f-core-2f-src

