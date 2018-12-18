#!/bin/bash

INSTALL=$PWD/install


install_func(){

mkdir -p ${INSTALL}

#进入目录进行编译安装
cd fdk-aac-0.1.6
chmod 777 configure config.sub config.guess
if test -s config.mak; then
    rm config.mak
fi

#配置
autoreconf -fiv
./configure --prefix=${INSTALL} --disable-shared

#编译并安装
make -j 8
if [[ $? -ne 0 ]]; then
    echo "fdk-aac make failed, Exit..."
    exit 1
fi
make install
if [[ $? -ne 0 ]]; then
    echo "fdk-aac install failed, Exit..."
    exit 1
fi

cd $INSTALL/..
if which tree; then
    tree $INSTALL
fi
}

uninstall_func(){

rm ${INSTALL} -rf

cd fdk-aac-0.1.6
make clean
make distclean

}

if [ $1 == 'all' ]
then
	echo 'install'
	install_func
fi

if [ $1 == 'clean' ]
then
	echo 'uninstall'
	uninstall_func
fi

