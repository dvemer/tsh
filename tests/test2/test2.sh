cp ../../tsh .
../.././run.sh&
sleep 1
TSH_PID=`pgrep '\<tsh'`
echo "PID:$TSH_PID"
cat output.fifo | tee output.txt&
../.././run_pts $TSH_PID input.txt
sleep 1
RESULT=`cmp output.txt output_valid.txt`
if [ "$RESULT" == "" ]; then
	echo "Passed"
fi
rm tsh
rm output.txt
