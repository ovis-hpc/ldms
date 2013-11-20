if [[ ! -d build/ ]]; then
	mkdir -p build/
fi;
cd build/
../configure --prefix=/opt/baler2 --with-sos=/opt/sos CFLAGS="-g -O0"
cd ../
echo "Please go into build/ to build the program"

