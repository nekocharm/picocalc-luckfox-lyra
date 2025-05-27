#!/bin/sh

USB0_IP=192.168.123.100
for i in $(seq 1 10);
do
    if ifconfig -a | grep -q 'usb0'; then
		ifconfig usb0 up

		USB0_CURRENT_MAC=$(cat /sys/class/net/usb0/address)

		if [ -f "$USB0_MAC_FILE" ]; then
			USB0_SAVED_MAC=$(cat "$USB0_MAC_FILE")
			if [ -z "$USB0_SAVED_MAC" ];then
				rm $USB0_MAC_FILE
				echo "$USB0_CURRENT_MAC" > "$USB0_MAC_FILE"
				echo "[usb-net-up] Saved current MAC address ($USB0_CURRENT_MAC) to $USB0_MAC_FILE"
			else
				echo "[usb-net-up] Saved usb0 Mac address:$USB0_SAVED_MAC"
				ifconfig usb0 down
				ifconfig usb0 hw ether "$USB0_SAVED_MAC"
				ifconfig usb0 up
				echo "[usb-net-up] Set usb0 MAC address to $USB0_SAVED_MAC"
			fi
		else
			echo "$USB0_CURRENT_MAC" > "$USB0_MAC_FILE"
			echo "[usb-net-up] Saved current MAC address ($USB0_CURRENT_MAC) to $USB0_MAC_FILE"
		fi

		ifconfig usb0 $USB0_IP
	fi

    if ifconfig -a | grep -q "$USB0_IP"; then
        exit
    fi

    sleep 5
done

