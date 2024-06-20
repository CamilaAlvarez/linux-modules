# DTS files

## Compiling

We can use ``dtc``.
````
dtc -@ -I input_format -O output_format -o output_file input_file
````

## Installing

To dynamically install an overlay we need to:

1. Make sure `configfs` is mounted

````
mount -t configfs non /sys/kernel/config 
````

2. Create folder for the overlay ``foo`` under the overlays folder

````
mkdir /sys/kernel/config/device-tree/overlay/foo
````

3. Load the overlay into the directory:

````
cat foo.dtbo > /sys/kernel/config/device-tree/overlay/foo/dtbo
````

4. To remove the overlay and undo the changes:

````
rmdir /sys/kernel/config/device-tree/overlay/foo
````
