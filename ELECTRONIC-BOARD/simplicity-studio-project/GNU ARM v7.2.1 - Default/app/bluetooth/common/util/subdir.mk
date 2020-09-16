################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../app/bluetooth/common/util/infrastructure.c 

OBJS += \
./app/bluetooth/common/util/infrastructure.o 

C_DEPS += \
./app/bluetooth/common/util/infrastructure.d 


# Each subdirectory must supply rules for building sources it contributes
app/bluetooth/common/util/infrastructure.o: ../app/bluetooth/common/util/infrastructure.c
	@echo 'Building file: $<'
	@echo 'Invoking: GNU ARM C Compiler'
	arm-none-eabi-gcc -g -gdwarf-2 -mcpu=cortex-m33 -mthumb -std=c99 '-DHAL_CONFIG=1' '-D__STACK_SIZE=0x800' '-DNVM3_DEFAULT_NVM_SIZE=24576' '-D__StackLimit=0x20000000' '-D__HEAP_SIZE=0xD00' '-DEFR32MG21A010F1024IM32=1' -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\platform\emlib\src" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\platform\Device\SiliconLabs\EFR32MG21\Include" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\app\bluetooth\common\util" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\hardware\kit\common\halconfig" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\platform\emdrv\nvm3\inc" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\platform\CMSIS\Include" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\hardware\kit\common\drivers" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\platform\emlib\inc" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\protocol\bluetooth\ble_stack\inc\common" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\platform\emdrv\sleep\inc" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\hardware\kit\common\bsp" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\platform\radio\rail_lib\common" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\platform\emdrv\common\inc" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\platform\Device\SiliconLabs\EFR32MG21\Source\GCC" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\platform\radio\rail_lib\protocol\ieee802154" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\protocol\bluetooth\ble_stack\inc\soc" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\platform\emdrv\uartdrv\inc" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\platform\emdrv\sleep\src" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\platform\emdrv\nvm3\src" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\platform\service\sleeptimer\src" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\platform\Device\SiliconLabs\EFR32MG21\Source" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\platform\radio\rail_lib\protocol\ble" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\platform\bootloader\api" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\platform\radio\rail_lib\chip\efr32\efr32xg2x" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\platform\emdrv\gpiointerrupt\inc" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\platform\service\sleeptimer\inc" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\platform\halconfig\inc\hal-config" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\platform\common\inc" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\platform\service\sleeptimer\config" -I"C:\Users\Kuba\SimplicityStudio\v4_workspace\soc-empty_2\platform\bootloader" -O2 -Wall -c -fmessage-length=0 -ffunction-sections -fdata-sections -mfpu=fpv5-sp-d16 -mfloat-abi=hard -MMD -MP -MF"app/bluetooth/common/util/infrastructure.d" -MT"app/bluetooth/common/util/infrastructure.o" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


