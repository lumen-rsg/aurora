#!/bin/bash

# This function is run from within the extracted source directory.
build() {
    ./autogen.sh
    ./configure --prefix=/usr
    make
}

# This function is run from within the extracted source directory,
# and is executed under fakeroot.
# The $PKG_DIR variable is set by aurora-build.
package() {
    make DESTDIR="$PKG_DIR" install
}