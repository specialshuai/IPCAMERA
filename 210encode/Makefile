.SUFFIXES : .c .o

OBJECTS = cam_encoder_test.o	\
		  ./SsbSipH264Encode.o\
		  ./Common/performance.o	\
		  ./Common/LogMsg.o			
		  
SRCS = $(OBJECTS:.o=.c)
DEPENDENCY = ./Common/lcd.h 			\
		     ./Common/LogMsg.h 			\
		     ./Common/mfc.h 			\
		     ./Common/MfcDriver.h 		\
		     ./Common/MfcDrvParams.h 	\
		     ./Common/performance.h 	\
			 ./Common/post.h			\
			 ./Common/videodev2.h		\
 		     ./SsbSipH264Encode.h\
	  	     ./cam_encoder_test.h					

CC = arm-linux-gcc
CFLAGS = -g -c -DLCD_SIZE_50 -Os -Wall
INC = -I./Common -I./FrameExtractor -I./MFC_API -I./JPEG_API

#KERNEL_PATH = /home/mini210/linux-3.7.4
#INC += -I$(KERNEL_PATH)/include

TARGET = multimedia_test

all : common multimedia_test install
common : 
		cd Common; $(MAKE)
$(TARGET) : $(OBJECTS)
		$(CC) -o $(TARGET) $(OBJECTS) -lpthread
install:
	cp multimedia_test /home/arm/

.c.o :
		$(CC) $(INC) $(CFLAGS) $<

clean :
		rm -rf $(OBJECTS) $(TARGET) core

