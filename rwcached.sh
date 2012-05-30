#!/bin/bash

PATH="/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin"

init_set()
{
#	echo "1048576" > /proc/sys/net/core/rmem_max
#	echo "1048576" > /proc/sys/net/core/rmem_default
	echo "init set ......."
}

service_stop()
{
	killall -s 3 rwcached
	echo "stoped ..."
}

service_start()
{
	cd /home/nekle/documents/rwcached
		./rwcached
}

case $1 in
	stop)
	echo "Service STOPING..........."
	service_stop
	;;
	start)
	echo "Service STARTING.........."
	init_set
	service_start
	;;
	restart)
	echo "Service RESTARTING........"
	service_stop
		sleep 1
	init_set
	service_start
	;;
	*)
	echo "Usage : $0 { stop | start | restart }"
esac

exit 0
