#!/usr/bin/env python


######################################
# basic file reader parser example
# for dynap-se, 3.1 AEDAT files 
# author federico.corradi@inilabs.com
######################################


import socket
import struct
import numpy as np
import time
import matplotlib
from matplotlib import pyplot as plt


#you will need to change this!!
filename = '/Users/USERNAME/data/dynp-se/caerOut-2016_11_20_10_40_45.aedat'
file_read = open(filename, "rb")
debug = False


def skip_header():
    ''' This function skip the standard header of the recording file '''
    line = file_read.readline()
    while line.startswith("#"):
        if ( line == '#!END-HEADER\r\n'):
            break
        else:
            line = file_read.readline()


def read_events():
    """ A simple function that read dynap-se events from cAER aedat 3.0 file format"""
    
    # raise Exception at end of file
    data = file_read.read(28)
    if(len(data) <= 0):
        print("read all data\n")
        raise NameError('END OF DATA')


    # read header
    eventtype = struct.unpack('H', data[0:2])[0]
    eventsource = struct.unpack('H', data[2:4])[0]
    eventsize = struct.unpack('I', data[4:8])[0]
    eventoffset = struct.unpack('I', data[8:12])[0]
    eventtsoverflow = struct.unpack('I', data[12:16])[0]
    eventcapacity = struct.unpack('I', data[16:20])[0]
    eventnumber = struct.unpack('I', data[20:24])[0]
    eventvalid = struct.unpack('I', data[24:28])[0]
    next_read = eventcapacity * eventsize  # we now read the full packet
    data = file_read.read(next_read)    
    counter = 0  # eventnumber[0]
    #spike events
    core_id_tot = []
    chip_id_tot = []
    neuron_id_tot = []
    ts_tot =[]
    #special events
    spec_type_tot =[]
    spec_ts_tot = []


    if(eventtype == 0):
        spec_type_tot =[]
        spec_ts_tot = []
        while(data[counter:counter + eventsize]):  # loop over all event packets
            special_data = struct.unpack('I', data[counter:counter + 4])[0]
            timestamp = struct.unpack('I', data[counter + 4:counter + 8])[0]
            spec_type = (special_data >> 1) & 0x0000007F
            spec_type_tot.append(spec_type)
            spec_ts_tot.append(timestamp)
            if(spec_type == 6 or spec_type == 7 or spec_type == 9 or spec_type == 10):
                print (timestamp, spec_type)
            counter = counter + eventsize        
    elif(eventtype == 12):
        while(data[counter:counter + eventsize]):  # loop over all event packets
            aer_data = struct.unpack('I', data[counter:counter + 4])[0]
            timestamp = struct.unpack('I', data[counter + 4:counter + 8])[0]
            core_id = (aer_data >> 1) & 0x0000001F
            chip_id = (aer_data >> 6) & 0x0000003F
            neuron_id = (aer_data >> 12) & 0x000FFFFF
            core_id_tot.append(core_id)
            chip_id_tot.append(chip_id)
            neuron_id_tot.append(neuron_id)
            ts_tot.append(timestamp)
            counter = counter + eventsize
            if(debug):          
                print("chip id "+str(chip_id)+'\n')
                print("core_id "+str(core_id)+'\n')
                print("neuron_id "+str(neuron_id)+'\n')
                print("timestamp "+str(timestamp)+'\n')
                print("####\n")


    return core_id_tot, chip_id_tot, neuron_id_tot, ts_tot, spec_type_tot, spec_ts_tot


if __name__ == '__main__':


    done_reading = False


    # skip comment header of file
    skip_header()
    
    # prepare lists
    core_id_tot = []
    chip_id_tot = []
    neuron_id_tot = []
    ts_tot  = []
    # special events
    spec_type_tot = []
    spec_ts_tot = []


    while(done_reading == False):
        try:
            core_id, chip_id, neuron_id, ts, spec_type, spec_ts = read_events()
            core_id_tot.extend(np.array(core_id))
            chip_id_tot.extend(np.array(chip_id))
            neuron_id_tot.extend(np.array(neuron_id))
            ts_tot.extend(np.array(ts))
            spec_type_tot.extend(np.array(spec_type))
            spec_ts_tot.extend(np.array(spec_ts))
        except NameError:
            file_read.close()
            done_reading = True


    
    # make all arrays
    core_id_tot = np.array(core_id_tot)
    chip_id_tot = np.array(chip_id_tot)
    neuron_id_tot = np.array(neuron_id_tot)
    ts_tot = np.array(ts_tot)


    # get the index for spikes coming from different cores 
    # we have only mapped a single chip in output, chip id 4. 
    # we do not care about chip_id
    indx_core_zero = np.where(np.transpose(core_id_tot)==0)[0]
    indx_core_one = np.where(np.transpose(core_id_tot)==1)[0]
    indx_core_two = np.where(np.transpose(core_id_tot)==2)[0]
    indx_core_three = np.where(np.transpose(core_id_tot)==3)[0]


    # plot raster 
    plt.plot(ts_tot[indx_core_zero], neuron_id_tot[indx_core_zero], 'rx')
    plt.plot(ts_tot[indx_core_one], neuron_id_tot[indx_core_one]+256, 'gx')
    plt.plot(ts_tot[indx_core_two], neuron_id_tot[indx_core_two]+(256*2), 'bx')
    plt.plot(ts_tot[indx_core_three], neuron_id_tot[indx_core_three]+(256*3), 'yx')
    plt.xlabel('Timestamp [us]')    
    plt.ylabel('Neruon id') 


    # show raster
    plt.show()

