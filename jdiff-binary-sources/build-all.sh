#!/bin/bash
set -e

SCRIPTPATH="$( cd "$(dirname "$0")" ; pwd -P )"

if [[ "$OSTYPE" == "darwin"* ]]; then
    SEDCMD="sed -i '' -e"
    LC_CTYPE=C
    LANG=C
else
    SEDCMD="sed -i -e"
fi

rm -rf $SCRIPTPATH/jdiff07/
rm -rf $SCRIPTPATH/jdiff081/
rm -rf $SCRIPTPATH/jdiff085/

echo "Extracting jdiff07..."
cd $SCRIPTPATH
rm -rf jdiff07
mkdir -p jdiff07
tar -xzf jdiff07.tgz -C jdiff07
mv jdiff07/jojodiff07/* jdiff07
rm -r jdiff07/jojodiff07/
echo "Extracting jdiff07 OK"
echo ""

echo "Extracting jdiff081..."
cd $SCRIPTPATH
rm -rf jdiff081
mkdir -p jdiff081
tar -xzf jdiff081.tgz -C jdiff081
mv jdiff081/jdiff081/* jdiff081
rm -r jdiff081/jdiff081/
echo "Extracting jdiff081 OK"
echo ""

echo "Extracting jdiff085..."
cd $SCRIPTPATH
rm -rf jdiff085
mkdir -p jdiff085
tar -xzf jdiff085.tgz -C jdiff085
mv jdiff085/jdiff085/* jdiff085
rm -r jdiff085/jdiff085/
echo "Extracting jdiff085 OK"
echo ""

echo "Building jdiff07..."
cd $SCRIPTPATH/jdiff07/src
$SEDCMD "s/GCC4=gcc-4/GCC4=gcc/g" Makefile      # patch out gcc-4
make -j`nproc`
mv jdiff.exe jdiff
if [ -f "Makefile''" ]; then
    rm "Makefile''"
fi
echo "Building jdiff07 OK"
echo ""

echo "Building jdiff081..."
cd $SCRIPTPATH/jdiff081/src
$SEDCMD "s/#define ulong unsigned long int//g" JDefs.h      # patch out unused ulong define
if [ -f "JDefs.h''" ]; then
    rm "JDefs.h''"
fi
make -j`nproc`
echo "Building jdiff081 OK"
echo ""

echo "Building jdiff085..."
cd $SCRIPTPATH/jdiff085/src
$SEDCMD "s/-m64//g" Makefile      # patch out -m64
if [ -f "Makefile''" ]; then
    rm "Makefile''"
fi
make -j`nproc`
echo "Building jdiff085 OK"
echo ""
