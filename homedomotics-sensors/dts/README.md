# DTS files

## Compiling

We can use ``dtc``.
```
dtc -I input_format -O output_format -o output_file input_file
```

## Installing

Note that overlay files are located in ``/boot/overlays`` and have an ``.dtbo`` extension.

After compiling the file we need to add the following line to ``/boot/config.txt``.
```
dtoverlay="dht11-module"
```
