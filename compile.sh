#!/usr/bin/env sh

SRCDIR="./src"
BUILDDIR="./build"
INSTALLDIR="/usr/local/bin"
BIN="triangle"

COMPILER="gcc"
FLAGS="-Wall -Wextra -Werror -pedantic-errors -Wfatal-errors"
LIBS="-lglfw -lvulkan -ldl -lpthread -lX11 -lXxf86vm -lXrandr -lXi"

case "$1" in
	"--debug")
		FLAGS="${FLAGS} -DDEBUG -g"
		;;
	"--clean")
		rm -rv $BUILDDIR
		exit $?
		;;
	"--uninstall")
		rm -v $INSTALLDIR/$BIN
		exit $?
		;;
	"--install")
		install -v -m755 ./$BIN $INSTALLDIR/$BIN
		exit $?
		;;
esac

if [ -d $BUILDDIR ]; then
	rm -rv $BUILDDIR/*
else
	mkdir -v $BUILDDIR
fi

for f in $SRCDIR/*.c; do
	$COMPILER $FLAGS -c -o $BUILDDIR/$(basename ${f%.*}).o $f
	echo "compiled '$f'"
done
$COMPILER $FLAGS -o $BIN $BUILDDIR/*.o $LIBS
