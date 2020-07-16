PID=$(cat supermercato.PID)
LOG="finalReport.log"

while [ -e /proc/$PID ]; do
	sleep 0.5
done

if [ -f $LOG ]; then
	while read line; do
		echo $line
	done < $LOG
else
	echo "File log mancante!"
fi
