target="*.h *.hpp *.c *.cpp *.cxx *.3 *.man *.info"
initfolder=`pwd`

for folder in $*
do
	if ! cd $folder 2> /dev/null
	then
		echo "cannot cd $folder, skip it"
		continue
	fi

	echo "changing in $folder..."
	sed -i 's/XDF_FIELD_/XDF_F_/g' $target 2> /dev/null
	sed -i 's/XDF_CHFIELD_/XDF_CF_/g' $target 2> /dev/null
	sed -i 's/XDF_CF_STORED_LABEL/XDF_CF_LABEL/g' $target 2> /dev/null
	sed -i 's/XDF_CF_PHYSICAL_/XDF_CF_P/g' $target 2> /dev/null
	sed -i 's/XDF_CF_DIGITAL_/XDF_CF_D/g' $target 2> /dev/null
	sed -i 's/XDF_CF_ARRAY_/XDF_CF_ARR/g' $target 2> /dev/null
	sed -i 's/XDF_CF_STORED_/XDF_CF_STO/g' $target 2> /dev/null
	sed -i 's/XDF_F_RECORD_/XDF_F_REC_/g' $target 2> /dev/null
	sed -i 's/XDF_F_NSAMPLE_PER_RECORD/XDF_F_REC_NSAMPLE/g' $target 2> /dev/null
	sed -i 's/XDF_F_REC_DESC/XDF_F_SESS_DESC/g' $target 2> /dev/null
	sed -i 's/XDF_CF_NONE/XDF_NOF/g' $target 2> /dev/null
	sed -i 's/XDF_F_NONE/XDF_NOF/g' $target 2> /dev/null

	cd $initfolder
done
echo "done"
