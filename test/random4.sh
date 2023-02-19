#!/bin/bash
alpha="ABCDEFGHIJKLMNOPQRSTUVWXYZ"
alnum="${alpha}0123456789"
d0=${alpha:$(($RANDOM % ${#alpha})):1}
d1=${alnum:$(($RANDOM % ${#alnum})):1}
d2=${alnum:$(($RANDOM % ${#alnum})):1}
d3=${alnum:$(($RANDOM % ${#alnum})):1}
echo "$d0$d1$d2$d3"
