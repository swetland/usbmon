CFLAGS := -O2 -g -Wall

usbmon: usbmon.o

clean:
	rm -f usbmon *.o
