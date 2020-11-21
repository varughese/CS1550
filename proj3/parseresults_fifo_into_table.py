import os
import csv

path = './fifo'

rows = []

files = {

}

for filename in os.listdir(path):
	with open(path + '/' + filename) as file:
		trace_name = filename.split(".")[0].split("_")[-1]
		row = [trace_name]
		for line in file:
			val = line.split(":")[-1].strip()
			header = line.split(":")[-2].split(" ")[-1].strip()
			row.append(val)
		rows.append(row)

print(rows)

with open("data_fifo.csv", 'w+', newline='') as myfile:
	wr = csv.writer(myfile, quoting=csv.QUOTE_ALL)
	for row in rows:
		wr.writerow(row)