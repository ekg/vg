export LIBRARY_PATH=`pwd`/lib:$LIBRARY_PATH
export LD_LIBRARY_PATH=`pwd`/lib:$LD_LIBRARY_PATH
export DYLD_LIBRARY_PATH=`pwd`/lib:$DYLD_LIBRARY_PATH
export LD_INCLUDE_PATH=`pwd`/include:$LD_INCLUDE_PATH
export C_INCLUDE_PATH=`pwd`/include:$C_INCLUDE_PATH
export CPLUS_INCLUDE_PATH=`pwd`/include:$CPLUS_INCLUDE_PATH
export INCLUDE_PATH=`pwd`/include:$INCLUDE_PATH
export PATH=`pwd`/bin:`pwd`/scripts:$PATH
export CC=$(which gcc)
export CXX=$(which g++)

#
#  disable until file arguments work as in normal bash :(
#
# add bash autocompletion
#if test -n "$BASH_VERSION"
#then
#
#	 . ./autocomp.bash
#fi
