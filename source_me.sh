export LIBRARY_PATH=`pwd`/lib:$LIBRARY_PATH
export LD_LIBRARY_PATH=`pwd`/lib:$LD_LIBRARY_PATH
export LD_INCLUDE_PATH=`pwd`/include:$LD_INCLUDE_PATH
export C_INCLUDE_PATH=`pwd`/include:$C_INCLUDE_PATH
export CPLUS_INCLUDE_PATH=`pwd`/include:$CPLUS_INCLUDE_PATH
export INCLUDE_PATH=`pwd`/include:$INCLUDE_PATH
export PATH=`pwd`/bin:$PATH
if [ ! -z ${CC} ];
then
	export CC=$(which gcc)
fi
if [ ! -z ${CXX} ];
then
	export CXX=$(which g++)
fi
