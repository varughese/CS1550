import os
import csv

path = './algo'

rows = []

files = {

}

for filename in os.listdir(path):
	with open(path + '/' + filename) as file:
		name = filename.split(".")[0]+".trace"
		if name not in files:
			files[name] = {
				'8': {
					'OPT_disk': 0,
					'OPT_faults': 0,
					'FIFO_disk': 0,
					'FIFO_faults': 0,
					'AGING_disk': 0,
					'AGING_faults': 0
				},
				'16': {
					'OPT_disk': 0,
					'OPT_faults': 0,
					'FIFO_disk': 0,
					'FIFO_faults': 0,
					'AGING_disk': 0,
					'AGING_faults': 0
				},
				'32': {
					'OPT_disk': 0,
					'OPT_faults': 0,
					'FIFO_disk': 0,
					'FIFO_faults': 0,
					'AGING_disk': 0,
					'AGING_faults': 0
				},
				'64': {
					'OPT_disk': 0,
					'OPT_faults': 0,
					'FIFO_disk': 0,
					'FIFO_faults': 0,
					'AGING_disk': 0,
					'AGING_faults': 0
				}
			}
		jawn = {}
		for line in file:
			val = line.split(":")[-1].strip()
			header = line.split(":")[-2].split(" ")[-1].strip()
			jawn[header] = val
		
		frames = jawn['frames']	
		files[name][frames][jawn['Algorithm']+"_"+"disk"] = jawn['disk']
		files[name][frames][jawn['Algorithm']+"_"+"faults"] = jawn['faults']	


rows = [['file_name', 'frames', 'opt disk writes', 'opt page_faults', 'fifo disk writes', 'fifo page_faults','aging disk writes', 'aging page_faults']]
for trace_name in files.keys():
	for frames in sorted([int(f) for f in files[trace_name].keys()]):
		row = []
		row.append(trace_name)
		row.append(frames)
		datas = files[trace_name][str(frames)]
		row.append(datas['OPT_disk'])
		row.append(datas['OPT_faults'])
		row.append(datas['FIFO_disk'])
		row.append(datas['FIFO_faults'])
		row.append(datas['AGING_faults'])
		row.append(datas['AGING_faults'])
		rows.append(row)
print(rows)

with open("data.csv", 'w+', newline='') as myfile:
	wr = csv.writer(myfile, quoting=csv.QUOTE_ALL)
	for row in rows:
		wr.writerow(row)