#!/bin/csh

foreach f (/usr/local/opt/opencv/lib/*.dylib)
   set bn=`basename $f`
   echo $bn
   install_name_tool -change /usr/local/opt/opencv/lib/$bn @executable_path/lib/$bn antidonuts
end
