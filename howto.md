This is the Howto of the driver.

I write this for the absolute beginners to ensure they can handle it. If someone is so smart
that he or she would omit it, just don't read further.

Hint: There are always a lot of ways to do things. I prefer to describe how it worked for me.
You can optimize it if you know how. If you find a wrong description I would be happy to hear from you.
github@schwarzers.de

* Preparation *
===============

* Get the kernel sources and build it. Get at least all headers.
----------------------------------------------------------------
==> This is distribution dependent and not my focus here
==> I have done it with Fedora 24 and this page helped me: http://www.hiroom2.com/2016/06/25/fedora-24-rebuild-kernel-with-src-rpm/

* Build environment
-------------------

==> as a normal user (not root)
==> Open a terminal (here we name it terminal#1)
==> Open another terminal (here we name it terminal#2)

==> In terminal#1
cd ~
mkdir git
cd git
git clone https://github.com/jesko42/minipci.git

* Make the driver *
===================

==> as a normal user (not root)
cd ~/git/linux/driver
make

* Show kernel messages
======================

==> kernel messages are shown with 'dmesg', we enhance that by updating
==> the output whenever something changes

==> In terminal#2
dmesg -wH

* Start the driver *
====================

==> In terminal#1
cd ~/git/linux/driver

==> We switch now to root, some people prefere the sudo command. It's up to you.
su
insmod minipci.ko

==> you should see the messages from minipci in terminal#2

exit

==> now you are a normal user again

* Make and start the test program *
===================================

==> In terminal#1
==> as a normal user (not root)
cd ~/git/linux/test
make

==> start

./minipcitest

==> For now (may change later) it opens up the driver,
==> accesses the BAR0 (Basic Address Range 0), write 16MB and
==> read back the 16MB. Then its comparead and 64kB are output to screen.
==> Look at the messages in terminal#2

* Unload the driver *
=====================

==> In terminal#1
cd ~/git/linux/driver

==> We switch now to root, some people prefere the sudo command. It's up to you.
su
rmmod minipci
exit

* Ready *
=========

