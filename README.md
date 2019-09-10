# GPIO boot reset

This project is using linux kernel legacy gpio to control reset and boot mode of any mcu.Tested on linux kernel version 3.18.71 and 4.15

## Setup

### Device tree 

In order to run this module, device tree must have node name **gpio-boot-reset**
The driver will parse flowing propreties of **gpio-boot-reset**'s child node

- **reset**: propreties that point to the that connect to reset pin of mcu (*mandatory*)
- **boot**: propreties that point to gpio that connect to boot pin of mcu (*mandatory*)
- **reset-time**: time of each reset pluse, default: 25 (unit: **us**) (*optional*)
- **boot-time**: time to hold boot pin after reset complete, default: 10 (unit: **us**) (*optional*)
- **reset-active-low**: boolean, determine reset gpio pin is active low (*optional*)
- **boot-active-low**: boolean, determine boot gpio pin is active low (*optional*)

__Example__:

```device tree
gpio-boot-reset{
    compatible = "gpio-boot-reset";
    status = "okay";

    pinctrl-0 = <&reset_pin_off>;
    stm32f042c4{
            reset = <&msm_gpio 95 0>;
            boot = <&msm_gpio 17 0>;
            reset-time = <20>;
            boot-time = <10>;
    };

    nfc {
            reset = <&msm_gpio 16 0>;
            boot = <&msm_gpio 93 0>;
            reset-time = <20>;
            boot-time = <3000>;
    };
};
```

example's  external pinctrl-0

```device tree
reset_pin { 
    qcom,pins = <&gp 95>, <&gp 17>, <&gp 16>, <&gp 93> ;

    qcom,num-grp-pins = <4>;
    label = "reset-gpio-pins";
    reset_pin_off: stm32f042c4_gpio_off {
        drive-strength = <2>;
        bias-disable; /* no pullup */
        output-low;
    };
};
```

## Reset partern  

This module only support 1 reset and boot partern only:

- Reset: reset high -> wait for *reset-time* -> reset low
- Boot: boot high -> reset high -> wair for *reset-time* > reset low -> wait for *boot-time* -> boot low

## Build

Provide your target kernel using KERNEL_SRC (default is host kernel directory **/lib/modules/$(shell uname -r )/build**) variable using following code

```
make KERNEL_SRC=/PATH/TO/YOUR/KERNEL/SOURCE
```

To clean unnecessary file

```
make clean
```

## Running

After install module into kernel, there will be 1 file attribute for each child node **/sys/class/gpio_boot_reset/*/mode**
To reset MCU use

```sh
echo -n "normal" > /sys/class/gpio_boot_reset/{child_node_name}/mode
```

To enter bootloader mode

```sh
echo -n "prog" > /sys/class/gpio_boot_reset/{child_node_name}/mode
```

With example device tree above, we will have 2 folder, which coresponse to 2 child node in device tree: **/sys/class/gpio_boot_reset/stm32f042c4/mode** and **/sys/class/gpio_boot_reset/nfc/mode**

## License

This project is licensed under the GPLv2 License - see the [LICENSE](./LICENSE) file for details
