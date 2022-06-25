import serial
import signal
import time
import os
import sh

from NotificationManager import NotificationManager
from Alarm import Alarm
from Logger import Logger

dirNameBrama = '/sharedfolders/MONITORING/brama_cam'
dirNameAltanka = '/sharedfolders/MONITORING/altanka_cam'

SMSDir = "/etc/scripts/SMS"

class Killer:
    kill_now = False
    def __init__(self):
        signal.signal(signal.SIGINT, self.exit_gracefully)
        signal.signal(signal.SIGTERM, self.exit_gracefully)
    
    def exit_gracefully(self, *args):
        self.kill_now = True


def splitSMS(fileAA):
    file = open(fileAA,"r")
    longSMS = file.read()
    file.close()
    SMSList = []
    smsTemp = ""
    for x in range(len(longSMS)):
        if x%150==0 and x>0:
            SMSList.append(smsTemp)
            smsTemp = ""
        else:
            smsTemp += longSMS[x]
    SMSList.append(smsTemp)

    i=1
    for message in SMSList:
        fileName="%s_%i"%(fileAA,i)
        newFile = open(fileName,"w")
        newFile.write(message)
        newFile.close()
        i+=1

    os.remove(fileAA)

def main():
    killer = Killer()

    Logger.settings(fileNameWihPath="/var/log/MonitoringManager.log",saveToFile=True,showFilename=True)
    global ALARMON
    ALARMON=False
    notificationManager = NotificationManager("/etc/scripts/active_users.txt")
    alarm = Alarm()
    counter=0
    counterAltanka=0
    counterBrama=0
    countFilesBrama=0
    countFilesAltanka=0
    theNewestDirAltanka = alarm.getTheNewestDayDir(dirNameAltanka)
    if(theNewestDirAltanka == 0): 
        notificationManager.sendSMSAdmin("Error with Disk")
        exit()

    theNewestDirBrama = alarm.getTheNewestDayDir(dirNameBrama)
    countFilesBrama = alarm.getListOfFiles(dirNameBrama+'/'+theNewestDirBrama)
    countFilesAltanka = alarm.getListOfFiles(dirNameAltanka+'/'+theNewestDirAltanka)
    Logger.INFO("Ready")
    while not killer.kill_now:
        notificationManager.readAT()
        if (counter > 20):
            counter=0
            newTheNewestDirAltanka = alarm.getTheNewestDayDir(dirNameAltanka)
            if(newTheNewestDirAltanka == 0):
                notificationManager.sendSMSAdmin("Error with Disk")
                exit()

            if (newTheNewestDirAltanka!= theNewestDirAltanka):  #new directory -> new day
                countFilesAltanka = 0
                theNewestDirAltanka=newTheNewestDirAltanka
            newCountFilesAltanka = alarm.getListOfFiles(dirNameAltanka+'/'+theNewestDirAltanka)

            if(newCountFilesAltanka-countFilesAltanka==2): # come in to loop, when somebody come in to possesion
                time.sleep(1.5)

            if(newCountFilesAltanka-countFilesAltanka>2):
                info = "ALARM ALTANKA"
                Logger.INFO(info)
                notificationManager.sendSMSNotification(info)
                alarm.alarmLog(info)
            countFilesAltanka=newCountFilesAltanka

            newTheNewestDirBrama = alarm.getTheNewestDayDir(dirNameBrama)
            if (newTheNewestDirBrama != theNewestDirBrama):
                countFilesBrama = 0
                theNewestDirBrama=newTheNewestDirBrama
            newCountFilesBrama = alarm.getListOfFiles(dirNameBrama+'/'+theNewestDirBrama)

            if(newCountFilesBrama-countFilesBrama>2):
                info = "ALARM BRAMA"
                Logger.INFO(info)
                notificationManager.sendSMSNotification(info)
                alarm.alarmLog(info)
            countFilesBrama=newCountFilesBrama
        
        counter=counter+1
       
        if notificationManager.readyToSMS:
            listSMSFiles = os.listdir(SMSDir)
            for x in listSMSFiles:
                smsFile = os.path.join(SMSDir, x)
                file = open(smsFile,"r")
                text = file.read()
                file.close()
                if len(text) > 150:
                    splitSMS(smsFile)
                    continue
                notificationManager.sendSMSAdmin(text)
                os.remove(smsFile)
                notificationManager.readyToSMS=False
                break # next sms on other cycle, when GSM will ready to SMS    
     
        time.sleep(0.5)

    notificationManager.phoneContacts.SaveToFile()
    notificationManager.saveToFile()
    Logger.INFO("exit program")

if __name__ == '__main__':
    main()