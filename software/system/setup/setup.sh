#!/bin/bash
# Basic operating system initalization of a new BeagleBone Black/Green/Green Wireless
# Usage: setup.sh [<hostname>]
# * <hostname> optionally specifies the the hostname to assign to the device
#   during setup, if not provided the default hostname used is: rocketlogger
#
# Copyright (c) 2016-2020, ETH Zurich, Computer Engineering Group
# All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 
# * Redistributions of source code must retain the above copyright notice, this
#   list of conditions and the following disclaimer.
# 
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# 
# * Neither the name of the copyright holder nor the names of its
#   contributors may be used to endorse or promote products derived from
#   this software without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
# 

HOSTNAME="rocketlogger"

# need to run as root
echo "> Check root permission"
if [[ $(id -u) -ne 0 ]]; then
  echo "Need to run as root. Aborting."
  exit 1
fi

# check that we run on a clean BeagelBone image
echo "> Check beaglebone platform"
if [[ "$(cat /etc/hostname)" != "beaglebone" ]]; then
  echo "Need to run this script on a clean BeagleBone image. Aborting."
  exit 1
fi

# check whether hostname argument is given
if [ $# -ge 1 ]; then
  HOSTNAME=$1
fi


## updates and software dependencies
echo "> Update system and install software dependencies"

# install buster-backports repository for meson and ninja
echo "deb http://deb.debian.org/debian buster-backports main" >> /etc/apt/sources.list.d/buster-backports.list

# update package lists
apt-get update

# install system packages
apt-get install --assume-yes        \
    curl                            \
    device-tree-compiler            \
    gcc                             \
    g++                             \
    git                             \
    make                            \
    meson/buster-backports          \
    ninja-build/buster-backports    \
    ntp                             \
    pkg-config                      \
    python3                         \
    python3-venv                    \
    ti-pru-cgt-installer            \
    libatlas-base-dev               \
    libgpiod-dev                    \
    libi2c-dev                      \
    libncurses5-dev                 \
    libzmq3-dev

# verify system dependencies installation was successful
INSTALL=$?
if [ $INSTALL -ne 0 ]; then
  echo "[ !! ] System dependencies installation failed (code $INSTALL). MANUALLY CHECK CONSOLE OUTPUT AND VERIFY SYSTEM CONFIGURATION."
  exit $INSTALL
else
  echo "[ OK ] System dependencies installation was successful."
fi

# install nodesource repository for nodejs 18.x LTS
curl --silent --location https://deb.nodesource.com/setup_18.x | bash -

# install nodejs
apt-get install --assume-yes        \
    nodejs


## default login
echo "> Create new user 'rocketlogger'"

# add new rocketlogger user with home directory and bash shell
useradd --create-home --shell /bin/bash rocketlogger
# set default password
cat user/password | chpasswd

# add rocketlogger user to sudo group
usermod --append --groups sudo rocketlogger

# display updated user configuration
id rocketlogger


## deactivate default login
echo "> Disable default user 'debian'"

# set expiration date in the past to disable logins
chage --expiredate 1970-01-01 debian


## user permission
echo "> Set user permissions"

# configure sudoers
cp --force sudo/privacy /etc/sudoers.d/
cp --force sudo/rocketlogger /etc/sudoers.d/
chmod 440 /etc/sudoers.d/*


## security
echo "> Update some security and permission settings"

# copy more secure ssh configuration
cp --force ssh/sshd_config /etc/ssh/
systemctl reload sshd

# copy public keys for log in
mkdir --parents /home/rocketlogger/.ssh/
chmod 700 /home/rocketlogger/.ssh/
cp --force user/rocketlogger.default_rsa.pub /home/rocketlogger/.ssh/
cat /home/rocketlogger/.ssh/rocketlogger.default_rsa.pub > /home/rocketlogger/.ssh/authorized_keys

# change ssh welcome message
cp --force system/issue.net /etc/issue.net


## filesystem, system and user config directories setup
echo "> Configure SD card mount and prepare configuration folders"

# external SD card mount
mkdir --parents /media/sdcard/
echo -e "# mount external sdcard on boot if available" >> /etc/fstab
echo -e "/dev/mmcblk0p1\t/media/sdcard/\tauto\tnofail,noatime,errors=remount-ro\t0\t2" >> /etc/fstab


# create RocketLogger system config folder
mkdir --parents /etc/rocketlogger

# user configuration and data folder for rocketlogger, bind sdcard folders if available
mkdir --parents /home/rocketlogger/.config/rocketlogger/
mkdir --parents /home/rocketlogger/data/
mkdir --parents /var/log/rocketlogger/
echo -e "# bind RocketLogger sdcard folders if available" >> /etc/fstab

echo -e "/media/sdcard/rocketlogger/config\t/home/rocketlogger/.config/rocketlogger\tauto\tbind,nofail,noatime,errors=remount-ro\t0\t0" >> /etc/fstab
echo -e "/media/sdcard/rocketlogger/data\t/home/rocketlogger/data\tauto\tbind,nofail,noatime,errors=remount-ro\t0\t0" >> /etc/fstab
echo -e "/media/sdcard/rocketlogger/log\t/var/log/rocketlogger\tauto\tbind,nofail,noatime,errors=remount-ro\t0\t0" >> /etc/fstab

# make user owner of its own files
chown --recursive rocketlogger:rocketlogger /home/rocketlogger/

# patch flasher tools to include SD card mounts
patch --forward --backup --input=tools/functions.sh.patch /opt/scripts/tools/eMMC/functions.sh


## network configuration
echo "> Update hostname and network configuration"

# change hostname
sed s/beaglebone/${HOSTNAME}/g --in-place /etc/hostname /etc/hosts

# copy network interface configuration
cp --force network/interfaces /etc/network/


## sync filesystem
echo "> Rocketlogger platform initialized. Syncing file system and exit."
sync
