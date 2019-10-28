#!/bin/bash
shopt -s globstar
gcov_data_dir="."
for x in ./**/*.o; do
  echo "x: $x"
  gcov "$gcov_data_dir/$x"
done


### Generate and copy gcov files ###

#export TARGET_DIR=
#export OUTPUT_DIR=
#
#cd "$TARGET_DIR"
##mkdir -p "$OUTPUT_DIR"
#
#CDIR=""
#for x in **/*.cpp; do
#  if [ "$CDIR" != "$TARGET_DIR/${x%/*}" ]; then
#    CDIR="$TARGET_DIR/${x%/*}"
#    cd $CDIR
#    gcov -p *.cpp
#
#    SUBDIR="${x%/*}"
#    PREFIX="#${SUBDIR/\//\#}"
#
#    for f in *.gcov; do
#        if [[ $f == \#* ]] ;
#        then
#           cp $f "$OUTPUT_DIR/$f"
#        else
#           cp $f "$OUTPUT_DIR/$PREFIX#$f"
#        fi
#    done
#  fi
#done