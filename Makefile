#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := esp32minifridge

flash: all

all: main/indexhtml.h 

all: main/fontttf.h 

all: main/fontsvg.h 

all: main/fonteot.h 

all: main/fontwoff.h 

all: main/wavdata.h 

all: main/certpem.h 

all: main/keypem.h


main/indexhtml.h: data/index.html
	python data2h.py data/index.html main/indexhtml.h

main/fontttf.h: data/material-design-icons.ttf
	python data2h.py data/material-design-icons.ttf main/fontttf.h
	
main/fontwoff.h: data/material-design-icons.woff
	python data2h.py data/material-design-icons.woff main/fontwoff.h
	
main/fontsvg.h: data/material-design-icons.svg
	python data2h.py data/material-design-icons.svg main/fontsvg.h

main/fonteot.h: data/material-design-icons.eot
	python data2h.py data/material-design-icons.eot main/fonteot.h

main/wavdata.h: data/gong.wav
	python data2h.py data/gong.wav main/wavdata.h

main/certpem.h: data/cert.pem
	python data2h.py data/cert.pem main/certpem.h

main/keypem.h: data/key.pem
	python data2h.py data/key.pem main/keypem.h


include $(IDF_PATH)/make/project.mk



