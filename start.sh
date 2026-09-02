#!/bin/sh

DIR=build/executables

APP=`ls -1t $DIR | head -1`

open ./$DIR/$APP
