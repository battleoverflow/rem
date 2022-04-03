#!/bin/bash

echo "Running the following compilation..."

# Creates C executable
make

dir="/opt/rem"

echo ""

if [ ! -d "$dir" ]
then
    # Requires root/sudo privileges
    sudo mkdir $dir
    echo "Directory $dir successfully created"
else
    echo "Directory $dir already exists"
fi


# Moves binary into the proper directory
sudo mv rem $dir