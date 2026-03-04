obj-m += ldp.o 
all:
	make -C /lib/modules/6.18.13-arch1-1/build/ M=$(PWD) modules
clean:
	make -C /lib/modules/6.18.13-arch1-1/build/ M=$(PWD) clean

