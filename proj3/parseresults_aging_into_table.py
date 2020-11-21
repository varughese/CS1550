import os
import csv

path = './rate'

rows = []

files = {

}

for filename in os.listdir(path):
	with open(path + '/' + filename) as file:
		trace_name = filename.split(".")[0].split("_")[-1]
		refresh_rate = filename.split(".")[1].split("_")[-1]
		row = [trace_name, refresh_rate]
		for line in file:
			val = line.split(":")[-1].strip()
			header = line.split(":")[-2].split(" ")[-1].strip()
			row.append(val)
		rows.append(row)


# rows = [['file_name', 'frames', 'opt disk writes', 'opt page_faults', 'fifo disk writes', 'fifo page_faults']]


with open("data_aging.csv", 'w+', newline='') as myfile:
	wr = csv.writer(myfile, quoting=csv.QUOTE_ALL)
	for row in rows:
		wr.writerow(row)