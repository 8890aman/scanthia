#!/usr/bin/env bash
export PATH=/usr/bin:/ucrt64/bin:$PATH
ls /ucrt64/bin | grep -iE "charls|openjp|jpeg|gdcm|itk" | head -30
echo ====gdcm-deps====
pacman -Qi mingw-w64-ucrt-x86_64-gdcm | grep -i depends
echo ====itk-deps====
pacman -Qi mingw-w64-ucrt-x86_64-itk | grep -i depends
echo ====itk-gdcm-libs====
ls /ucrt64/bin | grep -iE "itkgdcm|gdcm"
