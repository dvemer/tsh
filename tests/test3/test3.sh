gcc victim.c -o victim
cp ../../tsh .
../.././run.sh&
sleep 1
TSH_PID=`pgrep '\<tsh'`
echo "PID:$TSH_PID"
../.././run_pts $TSH_PID input.txt
sleep 1
echo "Passed"
rm tsh
rm victim
