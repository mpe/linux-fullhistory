#include <linux/config.h>
#ifdef CONFIG_USB_STORAGE_DEBUG

/* Debug output for Driver for USB mass storage (scsi-like) devices
 * 
 * (C) Michael Gee (michael@linuxspecific.com) 1999
 *
 */

void us_show_command(Scsi_Cmnd *srb)
{
	char *what = NULL;

	switch (srb->cmnd[0]) {
	case TEST_UNIT_READY: what = "TEST_UNIT_READY"; break;
	case REZERO_UNIT: what = "REZERO_UNIT"; break;
	case REQUEST_SENSE: what = "REQUEST_SENSE"; break;
	case FORMAT_UNIT: what = "FORMAT_UNIT"; break;
	case READ_BLOCK_LIMITS: what = "READ_BLOCK_LIMITS"; break;
	case REASSIGN_BLOCKS: what = "REASSIGN_BLOCKS"; break;
	case READ_6: what = "READ_6"; break;
	case WRITE_6: what = "WRITE_6"; break;
	case SEEK_6: what = "SEEK_6"; break;
	case READ_REVERSE: what = "READ_REVERSE"; break;
	case WRITE_FILEMARKS: what = "WRITE_FILEMARKS"; break;
	case SPACE: what = "SPACE"; break;
	case INQUIRY: what = "INQUIRY"; break;
	case RECOVER_BUFFERED_DATA: what = "RECOVER_BUFFERED_DATA"; break;
	case MODE_SELECT: what = "MODE_SELECT"; break;
	case RESERVE: what = "RESERVE"; break;
	case RELEASE: what = "RELEASE"; break;
	case COPY: what = "COPY"; break;
	case ERASE: what = "ERASE"; break;
	case MODE_SENSE: what = "MODE_SENSE"; break;
	case START_STOP: what = "START_STOP"; break;
	case RECEIVE_DIAGNOSTIC: what = "RECEIVE_DIAGNOSTIC"; break;
	case SEND_DIAGNOSTIC: what = "SEND_DIAGNOSTIC"; break;
	case ALLOW_MEDIUM_REMOVAL: what = "ALLOW_MEDIUM_REMOVAL"; break;
	case SET_WINDOW: what = "SET_WINDOW"; break;
	case READ_CAPACITY: what = "READ_CAPACITY"; break;
	case READ_10: what = "READ_10"; break;
	case WRITE_10: what = "WRITE_10"; break;
	case SEEK_10: what = "SEEK_10"; break;
	case WRITE_VERIFY: what = "WRITE_VERIFY"; break;
	case VERIFY: what = "VERIFY"; break;
	case SEARCH_HIGH: what = "SEARCH_HIGH"; break;
	case SEARCH_EQUAL: what = "SEARCH_EQUAL"; break;
	case SEARCH_LOW: what = "SEARCH_LOW"; break;
	case SET_LIMITS: what = "SET_LIMITS"; break;
	case READ_POSITION: what = "READ_POSITION"; break;
	case SYNCHRONIZE_CACHE: what = "SYNCHRONIZE_CACHE"; break;
	case LOCK_UNLOCK_CACHE: what = "LOCK_UNLOCK_CACHE"; break;
	case READ_DEFECT_DATA: what = "READ_DEFECT_DATA"; break;
	case MEDIUM_SCAN: what = "MEDIUM_SCAN"; break;
	case COMPARE: what = "COMPARE"; break;
	case COPY_VERIFY: what = "COPY_VERIFY"; break;
	case WRITE_BUFFER: what = "WRITE_BUFFER"; break;
	case READ_BUFFER: what = "READ_BUFFER"; break;
	case UPDATE_BLOCK: what = "UPDATE_BLOCK"; break;
	case READ_LONG: what = "READ_LONG"; break;
	case WRITE_LONG: what = "WRITE_LONG"; break;
	case CHANGE_DEFINITION: what = "CHANGE_DEFINITION"; break;
	case WRITE_SAME: what = "WRITE_SAME"; break;
	case READ_TOC: what = "READ_TOC"; break;
	case LOG_SELECT: what = "LOG_SELECT"; break;
	case LOG_SENSE: what = "LOG_SENSE"; break;
	case MODE_SELECT_10: what = "MODE_SELECT_10"; break;
	case MODE_SENSE_10: what = "MODE_SENSE_10"; break;
	case MOVE_MEDIUM: what = "MOVE_MEDIUM"; break;
	case READ_12: what = "READ_12"; break;
	case WRITE_12: what = "WRITE_12"; break;
	case WRITE_VERIFY_12: what = "WRITE_VERIFY_12"; break;
	case SEARCH_HIGH_12: what = "SEARCH_HIGH_12"; break;
	case SEARCH_EQUAL_12: what = "SEARCH_EQUAL_12"; break;
	case SEARCH_LOW_12: what = "SEARCH_LOW_12"; break;
	case READ_ELEMENT_STATUS: what = "READ_ELEMENT_STATUS"; break;
	case SEND_VOLUME_TAG: what = "SEND_VOLUME_TAG"; break;
	case WRITE_LONG_2: what = "WRITE_LONG_2"; break;
	default: break;
	}
	printk(KERN_DEBUG USB_STORAGE
	       "Command %s (%d bytes)\n", what, srb->cmd_len);
	printk(KERN_DEBUG USB_STORAGE 
	       "  %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
	       srb->cmnd[0], srb->cmnd[1], srb->cmnd[2], srb->cmnd[3], srb->cmnd[4], srb->cmnd[5], 
	       srb->cmnd[6], srb->cmnd[7], srb->cmnd[8], srb->cmnd[9]);
}

#endif
