import subprocess

for i in range(30):
    print("testing " + str(i+1) + " ...")
    output = subprocess.getoutput("make check_lab4")
    with open("test.txt", "a") as myfile:
        print("writing log...")
        myfile.write("test times: " + str(i+1)  + "\n")
        myfile.write(output[-200:-1])
        myfile.write('\n')
        myfile.write("test times: " + str(i+1)  + " done--------------------- \n")
        print("write log done")
    print("test " + str(i+1) + "done")
