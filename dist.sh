#!/bin/csh
rm -rf dist
mkdir dist
mkdir dist/lib
cp deploy.prototxt res10_300x300_ssd_iter_140000.caffemodel dist
cp /usr/local/opt/opencv/lib/*.dylib dist/lib
cp antidonuts dist
cd dist
foreach f (/usr/local/opt/opencv/lib/*.dylib)
   set bn=`basename $f`
   echo $bn
   install_name_tool -change /usr/local/opt/opencv/lib/$bn @executable_path/lib/$bn antidonuts
end
cd lib
chmod u+rw *
foreach l (*)
   set targetlib=`basename $l`
   echo $targetlib
   foreach f (/usr/local/opt/opencv/lib/*.dylib)
       set lib=`basename $f`
       install_name_tool -change @rpath/$lib @executable_path/lib/$lib $targetlib
       install_name_tool -change /usr/local/opt/opencv/lib/$lib @executable_path/lib/$lib $targetlib
   end
end

