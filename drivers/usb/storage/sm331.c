#include <linux/hdreg.h>
#include <linux/usb.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>

#include <linux/sm331_setup.h>
#include <linux/sdop_ioctl.h>

#include "usb.h"
#include "transport.h"
#include "protocol.h"
#include "debug.h"
#include "scsiglue.h"
#include "sm331.h"

#define REQUEST_SENSE_SIZE	18

//usb_stor_bulk_transfer_bufのタイムアウト時間
static int cmd_timeout = SD_USB_CMD_TIMEOUT;
module_param(cmd_timeout, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(cmd_timeout, " seconds to wait response of USB command.");

//書き込みリトライ回数
static int write_retry = SD_USB_WRITE_RETRY;
module_param(write_retry, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(write_retry, " times to retry WRITE.");

//読み込みリトライ回数
static int read_retry = SD_USB_READ_RETRY;
module_param(read_retry, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(read_retry, " times to retry READ.");

//最終リトライ時のリセットON/OFF
static int last_retry_reset = 0;
module_param(last_retry_reset, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(last_retry_reset, " : reset in last retry");

//書き込みエラー時のリセット処理可否
static int write_enable_reset = 1;
module_param(write_enable_reset, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(write_enable_reset, "  0 : unable, other : enable.");

//読み込みエラー時のリセット処理可否
static int read_enable_reset = 1;
module_param(read_enable_reset, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(read_enable_reset, "  0 : unable, other : enable.");

#define SDOP_DEBUG

#ifdef SDOP_DEBUG
//書き込みタイムエラー回数
static int write_timeout_num =  0;
module_param(write_timeout_num, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(write_timeout_num, " times, write timeout happen.");

//読み込みタイムエラー回数
static int read_timeout_num = 0;
module_param(read_timeout_num, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(read_timeout_num, " times, read timeout happen.");

//書き込みタイムエラー間隔
static int write_timeout_interval =  0;
module_param(write_timeout_interval, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(write_timeout_interval, " : write timeout interval.");

//読み込みタイムエラー間隔
static int read_timeout_interval = 0;
module_param(read_timeout_interval, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(read_timeout_interval, " : read timeout interval.");

DECLARE_WAIT_QUEUE_HEAD(timeout_wq);
#endif //SDOP_DEBUG

//Card Info構造体用メモリ確保関数
static alloc_card_info_t alloc_card_info = NULL;
inline void sm331_set_alloc_card_info(alloc_card_info_t func){ alloc_card_info = func; }

//Card Info構造体用メモリ解放関数
static free_card_info_t free_card_info = NULL;
inline void sm331_set_free_card_info(free_card_info_t func){ free_card_info = func; }

//Readコマンド発行関数
static read_transport_t read_transport = NULL;
inline void sm331_set_read_transport(read_transport_t func){ read_transport = func; }

//Writeコマンド発行関数
static write_transport_t write_transport = NULL;
inline void sm331_set_write_transport(write_transport_t func){ write_transport = func; }

//その他のコマンド発行関数
static other_transport_t other_transport = NULL;
inline void sm331_set_other_transport(other_transport_t func){ other_transport = func; }

//Writeコマンド分配関数
static alignment_write_t alignment_write = NULL;
inline void sm331_set_alignment_write(alignment_write_t func){ alignment_write = func; }

//Writeリトライ関数
static retry_write_t retry_write = NULL;
inline void sm331_set_retry_write(retry_write_t func){ retry_write = func; }

//エラー通知関数
static notify_error_t notify_error = NULL;
inline void sm331_set_notify_error(notify_error_t func){ notify_error = func; }

//デバイスリセット関数
static reset_device_t reset_device = NULL;
inline void sm331_set_reset_device(reset_device_t func){ reset_device = func; }

//エラー要因チェック関数
static request_sense_t request_sense = NULL;
inline void sm331_set_request_sense(request_sense_t func){ request_sense = func; }

//マウント通知関数
static notify_mount_t notify_mount = NULL;
inline void sm331_set_notify_mount(notify_mount_t func){ notify_mount = func; }

//アンマウント通知関数
static notify_umount_t notify_umount = NULL;
inline void sm331_set_notify_umount(notify_umount_t func){ notify_umount = func; }

//関数設定完了通知
static int set_func = 0;
inline void sm331_set_func_status(int status){ set_func = status; }

//最終リトライ時のリセットON/OFFフラグ取得関数
inline int sm331_is_last_retry_reset(){ return last_retry_reset; }

//書き込みエラー時のリセット処理可否取得関数
inline int sm331_is_write_reset_on(){ return write_enable_reset; }

//読み込みエラー時のリセット処理可否取得関数
inline int sm331_is_read_reset_on(){ return read_enable_reset; }

EXPORT_SYMBOL(sm331_set_alloc_card_info);
EXPORT_SYMBOL(sm331_set_free_card_info);
EXPORT_SYMBOL(sm331_set_read_transport);
EXPORT_SYMBOL(sm331_set_write_transport);
EXPORT_SYMBOL(sm331_set_other_transport);
EXPORT_SYMBOL(sm331_set_alignment_write);
EXPORT_SYMBOL(sm331_set_retry_write);
EXPORT_SYMBOL(sm331_set_notify_error);
EXPORT_SYMBOL(sm331_set_reset_device);
EXPORT_SYMBOL(sm331_set_request_sense);
EXPORT_SYMBOL(sm331_set_notify_mount);
EXPORT_SYMBOL(sm331_set_notify_umount);
EXPORT_SYMBOL(sm331_set_func_status);
EXPORT_SYMBOL(sm331_is_last_retry_reset);
EXPORT_SYMBOL(sm331_is_write_reset_on);
EXPORT_SYMBOL(sm331_is_read_reset_on);

static int sm331_mount_fs(struct scsi_device *sdev);
static int sm331_umount_fs(struct scsi_device *sdev);

static struct scsi_notify_operations scsi_ops = {
	.mount_fs	= sm331_mount_fs,
	.umount_fs	= sm331_umount_fs,
};

/**
* @fn		__sm331_normal_transport
* @brief	transport SCSI Command.
* @param	srb : SCSI command structure
* @param	us  : USB Storage device structure
* @return	success : 0	failure : not zero
* @date		2008/07/10
*/
static int __sm331_normal_transport(struct scsi_cmnd *srb, struct us_data *us)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	struct bulk_cs_wrap *bcs = (struct bulk_cs_wrap *) us->iobuf;
	unsigned int transfer_length = scsi_bufflen(srb);
	unsigned int residue;
	int result;
	int fake_sense = 0;
	unsigned int cswlen;
	unsigned int cbwlen = US_BULK_CB_WRAP_LEN;
#ifdef SDOP_DEBUG
	static int write_count = 0;
	static int read_count = 0;
#endif //SDOP_DUBUG

	/* Take care of BULK32 devices; set extra byte to 0 */
	if (unlikely(us->fflags & US_FL_BULK32)) {
		cbwlen = 32;
		us->iobuf[31] = 0;
	}

	/* set up the command wrapper */
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength = cpu_to_le32(transfer_length);
	bcb->Flags = srb->sc_data_direction == DMA_FROM_DEVICE ? 1 << 7 : 0;
	bcb->Tag = ++us->tag;
	bcb->Lun = srb->device->lun;
	if (us->fflags & US_FL_SCM_MULT_TARG)
		bcb->Lun |= srb->device->id << 4;
	bcb->Length = srb->cmd_len;

	/* copy the command payload */
	memset(bcb->CDB, 0, sizeof(bcb->CDB));
	memcpy(bcb->CDB, srb->cmnd, bcb->Length);

	/* send it to out endpoint */
	US_DEBUGP("Bulk Command S 0x%x T 0x%x L %d F %d Trg %d LUN %d CL %d\n",
			le32_to_cpu(bcb->Signature), bcb->Tag,
			le32_to_cpu(bcb->DataTransferLength), bcb->Flags,
			(bcb->Lun >> 4), (bcb->Lun & 0x0F), 
			bcb->Length);
	result = __usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe,
				bcb, cbwlen, NULL, cmd_timeout * HZ);
	US_DEBUGP("Bulk command transfer result=%d\n", result);
	if (result == USB_STOR_XFER_TIMEOUT)
		return SM331_TRANS_TIMEOUT;
	else if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;
#ifdef SDOP_DEBUG
	if(srb->cmnd[0] == WRITE_10 && write_timeout_num){
		printk("write : dummy timeout : %d sec\n", cmd_timeout);
		wait_event_timeout(timeout_wq, 0, cmd_timeout*HZ);
		write_timeout_num--;
		return SM331_TRANS_TIMEOUT;
	}
	else if(srb->cmnd[0] == READ_10 && read_timeout_num){
		printk("read : dummy timeout : %d sec\n", cmd_timeout);
		wait_event_timeout(timeout_wq, 0, cmd_timeout*HZ);
		read_timeout_num--;
		return SM331_TRANS_TIMEOUT;
	}
	else if(srb->cmnd[0] == WRITE_10 && write_timeout_interval){
		write_count++;

		if(write_count >= write_timeout_interval){
			printk("write : dummy timeout : %d sec\n", cmd_timeout);
			wait_event_timeout(timeout_wq, 0, cmd_timeout*HZ);
			write_count = 0;
			return SM331_TRANS_TIMEOUT;
		}
	}
	else if(srb->cmnd[0] == READ_10 && read_timeout_interval){
		read_count++;

		if(read_count >= read_timeout_interval){
			printk("read : dummy timeout : %d sec\n", cmd_timeout);
			wait_event_timeout(timeout_wq, 0, cmd_timeout*HZ);
			read_count = 0;
			return SM331_TRANS_TIMEOUT;
		}
	}
#endif //SDOP_DEBUG

	/* DATA STAGE */
	/* send/receive data payload, if there is any */

	/* Some USB-IDE converter chips need a 100us delay between the
	 * command phase and the data phase.  Some devices need a little
	 * more than that, probably because of clock rate inaccuracies. */
	if (unlikely(us->fflags & US_FL_GO_SLOW))
		udelay(125);

	if (transfer_length) {
		unsigned int pipe = srb->sc_data_direction == DMA_FROM_DEVICE ? 
				us->recv_bulk_pipe : us->send_bulk_pipe;

		result = usb_stor_bulk_srb(us, pipe, srb);
		US_DEBUGP("Bulk data transfer result 0x%x\n", result);
		if (result == USB_STOR_XFER_ERROR)
			return USB_STOR_TRANSPORT_ERROR;

		/* If the device tried to send back more data than the
		 * amount requested, the spec requires us to transfer
		 * the CSW anyway.  Since there's no point retrying the
		 * the command, we'll return fake sense data indicating
		 * Illegal Request, Invalid Field in CDB.
		 */
		if (result == USB_STOR_XFER_LONG)
			fake_sense = 1;
	}

	/* See flow chart on pg 15 of the Bulk Only Transport spec for
	 * an explanation of how this code works.
	 */

	/* get CSW for device status */
	US_DEBUGP("Attempting to get CSW...\n");
	result = __usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
				bcs, US_BULK_CS_WRAP_LEN, &cswlen, cmd_timeout * HZ);

	/* Some broken devices add unnecessary zero-length packets to the
	 * end of their data transfers.  Such packets show up as 0-length
	 * CSWs.  If we encounter such a thing, try to read the CSW again.
	 */
	if (result == USB_STOR_XFER_SHORT && cswlen == 0) {
		US_DEBUGP("Received 0-length CSW; retrying...\n");
		result = __usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
				bcs, US_BULK_CS_WRAP_LEN, &cswlen, cmd_timeout * HZ);
	}

	/* did the attempt to read the CSW fail? */
	if (result == USB_STOR_XFER_STALLED) {

		/* get the status again */
		US_DEBUGP("Attempting to get CSW (2nd try)...\n");
		result = __usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
				bcs, US_BULK_CS_WRAP_LEN, NULL, cmd_timeout * HZ);
	}

	/* if we still have a failure at this point, we're in trouble */
	US_DEBUGP("Bulk status result = %d\n", result);
	if (result == USB_STOR_XFER_TIMEOUT)
		return SM331_TRANS_TIMEOUT;
	else if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;
#ifdef SDOP_DEBUG
	if(srb->cmnd[0] == WRITE_10 && write_timeout_num){
		write_timeout_num--;
		wait_event_timeout(timeout_wq, 0, cmd_timeout*HZ);
		return SM331_TRANS_TIMEOUT;
	}
	else if(srb->cmnd[0] == READ_10 && read_timeout_num){
		read_timeout_num--;
		wait_event_timeout(timeout_wq, 0, cmd_timeout*HZ);
		return SM331_TRANS_TIMEOUT;
	}
	else if(srb->cmnd[0] == WRITE_10 && write_timeout_interval){
		write_count++;

		if(write_count >= write_timeout_interval){
			printk("write : dummy timeout : %d sec\n", cmd_timeout);
			wait_event_timeout(timeout_wq, 0, cmd_timeout*HZ);
			write_count = 0;
			return SM331_TRANS_TIMEOUT;
		}
	}
	else if(srb->cmnd[0] == READ_10 && read_timeout_interval){
		read_count++;

		if(read_count >= read_timeout_interval){
			printk("read : dummy timeout : %d sec\n", cmd_timeout);
			wait_event_timeout(timeout_wq, 0, cmd_timeout*HZ);
			read_count = 0;
			return SM331_TRANS_TIMEOUT;
		}
	}
#endif //SDOP_DEBUG

	/* check bulk status */
	residue = le32_to_cpu(bcs->Residue);
	US_DEBUGP("Bulk Status S 0x%x T 0x%x R %u Stat 0x%x\n",
			le32_to_cpu(bcs->Signature), bcs->Tag, 
			residue, bcs->Status);
	if (!(bcs->Tag == us->tag || (us->fflags & US_FL_BULK_IGNORE_TAG)) ||
		bcs->Status > US_BULK_STAT_PHASE) {
		US_DEBUGP("Bulk logical error\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	/* Some broken devices report odd signatures, so we do not check them
	 * for validity against the spec. We store the first one we see,
	 * and check subsequent transfers for validity against this signature.
	 */
	if (!us->bcs_signature) {
		us->bcs_signature = bcs->Signature;
		if (us->bcs_signature != cpu_to_le32(US_BULK_CS_SIGN))
			US_DEBUGP("Learnt BCS signature 0x%08X\n",
					le32_to_cpu(us->bcs_signature));
	} else if (bcs->Signature != us->bcs_signature) {
		US_DEBUGP("Signature mismatch: got %08X, expecting %08X\n",
			  le32_to_cpu(bcs->Signature),
			  le32_to_cpu(us->bcs_signature));
		return USB_STOR_TRANSPORT_ERROR;
	}

	/* try to compute the actual residue, based on how much data
	 * was really transferred and what the device tells us */
	if (residue && !(us->fflags & US_FL_IGNORE_RESIDUE)) {

		/* Heuristically detect devices that generate bogus residues
		 * by seeing what happens with INQUIRY and READ CAPACITY
		 * commands.
		 */
		if (bcs->Status == US_BULK_STAT_OK &&
				scsi_get_resid(srb) == 0 &&
					((srb->cmnd[0] == INQUIRY &&
						transfer_length == 36) ||
					(srb->cmnd[0] == READ_CAPACITY &&
						transfer_length == 8))) {
			us->fflags |= US_FL_IGNORE_RESIDUE;

		} else {
			residue = min(residue, transfer_length);
			scsi_set_resid(srb, max(scsi_get_resid(srb),
			                                       (int) residue));
		}
	}

	/* based on the status code, we report good or bad */
	switch (bcs->Status) {
		case US_BULK_STAT_OK:
			/* device babbled -- return fake sense data */
			if (fake_sense) {
				memcpy(srb->sense_buffer, 
				       usb_stor_sense_invalidCDB, 
				       sizeof(usb_stor_sense_invalidCDB));
				return USB_STOR_TRANSPORT_NO_SENSE;
			}

			/* command good -- note that data could be short */
			return USB_STOR_TRANSPORT_GOOD;

		case US_BULK_STAT_FAIL:
			/* command failed */
			return USB_STOR_TRANSPORT_FAILED;

		case US_BULK_STAT_PHASE:
			/* phase error -- note that a transport reset will be
			 * invoked by the invoke_transport() function
			 */
			return USB_STOR_TRANSPORT_ERROR;
	}

	/* we should never get here, but if we do, we're in trouble */
	return USB_STOR_TRANSPORT_ERROR;
}

/**
* @fn		sm331_normal_transport_with_status
* @brief	transport SCSI Command.
* @param	srb	: SCSI command structure
* @param	us	: USB Storage device structure
* @param	status	: Error status
* @return	success : 0	failure : not zero
* @date		2008/07/10
*/
static int sm331_normal_transport_with_status(struct scsi_cmnd *srb, struct us_data *us, int *status)
{
	int ret;

	ret = __sm331_normal_transport(srb, us);

	*status = ret;
	if((srb->cmnd[0] != REQUEST_SENSE)
			&& !(srb->cmnd[0] == 0xF0 && srb->cmnd[1] == 0x2C)){
		if(set_func && request_sense){
			request_sense(srb, us->extra, status);
		}
	}

	return ret;
}

/**
* @fn		sm331_normal_transport
* @brief	transport SCSI Command.
* @param	srb	: SCSI command structure
* @param	us	: USB Storage device structure
* @return	success : 0	failure : not zero
* @date		2008/07/10
*/
static int sm331_normal_transport(struct scsi_cmnd *srb, struct us_data *us)
{
	int status = 0;

	return sm331_normal_transport_with_status(srb, us, &status);
}

/**
* @fn		sm331_card_info_destructor
* @brief	destructor of extra data in us_data.
* @param	extra : extra data pointer
* @param	reset_err : 0 not reset err flag, other reset err flag.
* @return	void
* @date		2008/07/10
*/
static void sm331_card_info_destructor(void *extra, int reset_err)
{
	if(unlikely(!set_func || (free_card_info == NULL)))
		return;

	free_card_info(extra, reset_err);
}

/**
* @fn		__sm331_card_info_destructor
* @brief	destructor of extra data in us_data.
* @param	extra : extra data pointer
* @return	void
* @date		2009/03/04
*/
static void __sm331_card_info_destructor(void *extra)
{
	sm331_card_info_destructor(extra, 0);
}

/**
* @fn		__sm331_internal_cmd
* @brief	transport SCSI Command from this driver.
* @param	srb	    : SCSI command structure
* @param	us_data	    : USB Storage device structure
* @param	cmnd	    : Command Description
* @param	cmnd_length : Length of Command Description
* @param	reply_buf   : Reply buffer
* @param	reply_len   : Length of reply buffer
* @param	direction   : DMA_BIDIRECTIONAL, DMA_TO_DEVICE, DMA_FROM_DEVICE, DMA_NONE
* @return	success : 0	failure : not zero
* @date		2008/07/10
*/
int __sm331_internal_cmd(
	struct scsi_cmnd *srb, void *us_data, __u8 *cmnd, __u8 cmnd_length,
	void *reply_buf, unsigned int reply_len, int direction)
{
	struct bulk_cb_wrap bcb;
	struct bulk_cs_wrap bcs;
	unsigned int transfer_length = reply_len;
	unsigned int residue;
	int resid;
	int result;
	int fake_sense = 0;
	unsigned int cswlen;
	unsigned int cbwlen = US_BULK_CB_WRAP_LEN;
	struct us_data *us = (struct us_data *)us_data;
#ifdef SDOP_DEBUG
	static int write_count = 0;
	static int read_count = 0;
#endif //SDOP_DUBUG

	/* set up the command wrapper */
	bcb.Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb.DataTransferLength = cpu_to_le32(transfer_length);
	bcb.Flags = direction == DMA_FROM_DEVICE ? 1 << 7 : 0;
	bcb.Tag = ++us->tag;
	bcb.Lun = srb->device->lun;
	if (us->fflags & US_FL_SCM_MULT_TARG)
		bcb.Lun |= srb->device->id << 4;
	bcb.Length = cmnd_length;

	/* copy the command payload */
	memset(bcb.CDB, 0, sizeof(bcb.CDB));
	memcpy(bcb.CDB, cmnd, cmnd_length);

	/* send it to out endpoint */
	US_DEBUGP("Bulk Command S 0x%x T 0x%x L %d F %d Trg %d LUN %d CL %d\n",
			le32_to_cpu(bcb.Signature), bcb.Tag,
			le32_to_cpu(bcb.DataTransferLength), bcb.Flags,
			(bcb.Lun >> 4), (bcb.Lun & 0x0F), 
			bcb.Length);
	result = __usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe,
				&bcb, cbwlen, NULL, cmd_timeout * HZ);
	US_DEBUGP("Bulk command transfer result=%d\n", result);
	if (result == USB_STOR_XFER_TIMEOUT)
		return SM331_TRANS_TIMEOUT;
	else if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;
#ifdef SDOP_DEBUG
	if(cmnd[0] == WRITE_10 && write_timeout_num){
		write_timeout_num--;
		wait_event_timeout(timeout_wq, 0, cmd_timeout*HZ);
		return SM331_TRANS_TIMEOUT;
	}
	else if(cmnd[0] == READ_10 && read_timeout_num){
		read_timeout_num--;
		wait_event_timeout(timeout_wq, 0, cmd_timeout*HZ);
		return SM331_TRANS_TIMEOUT;
	}
	else if(cmnd[0] == WRITE_10 && write_timeout_interval){
		write_count++;

		if(write_count >= write_timeout_interval){
			printk("write : dummy timeout : %d sec\n", cmd_timeout);
			wait_event_timeout(timeout_wq, 0, cmd_timeout*HZ);
			write_count = 0;
			return SM331_TRANS_TIMEOUT;
		}
	}
	else if(cmnd[0] == READ_10 && read_timeout_interval){
		read_count++;

		if(read_count >= read_timeout_interval){
			printk("read : dummy timeout : %d sec\n", cmd_timeout);
			wait_event_timeout(timeout_wq, 0, cmd_timeout*HZ);
			read_count = 0;
			return SM331_TRANS_TIMEOUT;
		}
	}
#endif //SDOP_DEBUG

	/* DATA STAGE */
	/* send/receive data payload, if there is any */

	/* Some USB-IDE converter chips need a 100us delay between the
	 * command phase and the data phase.  Some devices need a little
	 * more than that, probably because of clock rate inaccuracies. */
	if (unlikely(us->fflags & US_FL_GO_SLOW))
		udelay(125);

	if (transfer_length) {
		unsigned int pipe = direction == DMA_FROM_DEVICE ? 
				us->recv_bulk_pipe : us->send_bulk_pipe;

//		result = usb_stor_bulk_srb(us, pipe, srb);
		result = usb_stor_bulk_transfer_sg(us, pipe,
					reply_buf, transfer_length,
					0, &resid);
		US_DEBUGP("Bulk data transfer result 0x%x\n", result);
		if (result == USB_STOR_XFER_ERROR)
			return USB_STOR_TRANSPORT_ERROR;

		/* If the device tried to send back more data than the
		 * amount requested, the spec requires us to transfer
		 * the CSW anyway.  Since there's no point retrying the
		 * the command, we'll return fake sense data indicating
		 * Illegal Request, Invalid Field in CDB.
		 */
		if (result == USB_STOR_XFER_LONG)
			fake_sense = 1;
	}

	/* See flow chart on pg 15 of the Bulk Only Transport spec for
	 * an explanation of how this code works.
	 */

	/* get CSW for device status */
	US_DEBUGP("Attempting to get CSW...\n");
	result = __usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
				&bcs, US_BULK_CS_WRAP_LEN, &cswlen, cmd_timeout * HZ);

	/* Some broken devices add unnecessary zero-length packets to the
	 * end of their data transfers.  Such packets show up as 0-length
	 * CSWs.  If we encounter such a thing, try to read the CSW again.
	 */
	if (result == USB_STOR_XFER_SHORT && cswlen == 0) {
		US_DEBUGP("Received 0-length CSW; retrying...\n");
		result = __usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
				&bcs, US_BULK_CS_WRAP_LEN, &cswlen, cmd_timeout * HZ);
	}

	/* did the attempt to read the CSW fail? */
	if (result == USB_STOR_XFER_STALLED) {

		/* get the status again */
		US_DEBUGP("Attempting to get CSW (2nd try)...\n");
		result = __usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
				&bcs, US_BULK_CS_WRAP_LEN, NULL, cmd_timeout * HZ);
	}

	/* if we still have a failure at this point, we're in trouble */
	US_DEBUGP("Bulk status result = %d\n", result);
	if (result == USB_STOR_XFER_TIMEOUT)
		return SM331_TRANS_TIMEOUT;
	else if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;
#ifdef SDOP_DEBUG
	if(cmnd[0] == WRITE_10 && write_timeout_num){
		write_timeout_num--;
		wait_event_timeout(timeout_wq, 0, cmd_timeout*HZ);
		return SM331_TRANS_TIMEOUT;
	}
	else if(cmnd[0] == READ_10 && read_timeout_num){
		read_timeout_num--;
		wait_event_timeout(timeout_wq, 0, cmd_timeout*HZ);
		return SM331_TRANS_TIMEOUT;
	}
	else if(cmnd[0] == WRITE_10 && write_timeout_interval){
		write_count++;

		if(write_count >= write_timeout_interval){
			printk("write : dummy timeout : %d sec\n", cmd_timeout);
			wait_event_timeout(timeout_wq, 0, cmd_timeout*HZ);
			write_count = 0;
			return SM331_TRANS_TIMEOUT;
		}
	}
	else if(cmnd[0] == READ_10 && read_timeout_interval){
		read_count++;

		if(read_count >= read_timeout_interval){
			printk("read : dummy timeout : %d sec\n", cmd_timeout);
			wait_event_timeout(timeout_wq, 0, cmd_timeout*HZ);
			read_count = 0;
			return SM331_TRANS_TIMEOUT;
		}
	}
#endif //SDOP_DEBUG

	/* check bulk status */
	residue = le32_to_cpu(bcs.Residue);
	US_DEBUGP("Bulk Status S 0x%x T 0x%x R %u Stat 0x%x\n",
			le32_to_cpu(bcs.Signature), bcs.Tag, 
			residue, bcs.Status);
	if (!(bcs.Tag == us->tag || (us->fflags & US_FL_BULK_IGNORE_TAG)) ||
		bcs.Status > US_BULK_STAT_PHASE) {
		US_DEBUGP("Bulk logical error\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	/* Some broken devices report odd signatures, so we do not check them
	 * for validity against the spec. We store the first one we see,
	 * and check subsequent transfers for validity against this signature.
	 */
	if (!us->bcs_signature) {
		us->bcs_signature = bcs.Signature;
		if (us->bcs_signature != cpu_to_le32(US_BULK_CS_SIGN))
			US_DEBUGP("Learnt BCS signature 0x%08X\n",
					le32_to_cpu(us->bcs_signature));
	} else if (bcs.Signature != us->bcs_signature) {
		US_DEBUGP("Signature mismatch: got %08X, expecting %08X\n",
			  le32_to_cpu(bcs.Signature),
			  le32_to_cpu(us->bcs_signature));
		return USB_STOR_TRANSPORT_ERROR;
	}

	/* try to compute the actual residue, based on how much data
	 * was really transferred and what the device tells us */
	if (residue && !(us->fflags & US_FL_IGNORE_RESIDUE)) {

		/* Heuristically detect devices that generate bogus residues
		 * by seeing what happens with INQUIRY and READ CAPACITY
		 * commands.
		 */
		if (bcs.Status == US_BULK_STAT_OK &&
				scsi_get_resid(srb) == 0 &&
					((cmnd[0] == INQUIRY &&
						transfer_length == 36) ||
					(cmnd[0] == READ_CAPACITY &&
						transfer_length == 8))) {
			us->fflags |= US_FL_IGNORE_RESIDUE;

		} else {
			residue = min(residue, transfer_length);
//			scsi_set_resid(srb, max(scsi_get_resid(srb),
//			                                       (int) residue));
		}
	}

	/* based on the status code, we report good or bad */
	switch (bcs.Status) {
		case US_BULK_STAT_OK:
			/* device babbled -- return fake sense data */
			if (fake_sense) {
				return USB_STOR_TRANSPORT_NO_SENSE;
			}

			/* command good -- note that data could be short */
			return USB_STOR_TRANSPORT_GOOD;

		case US_BULK_STAT_FAIL:
			/* command failed */
			return USB_STOR_TRANSPORT_FAILED;

		case US_BULK_STAT_PHASE:
			/* phase error -- note that a transport reset will be
			 * invoked by the invoke_transport() function
			 */
			return USB_STOR_TRANSPORT_ERROR;
	}

	/* we should never get here, but if we do, we're in trouble */
	return USB_STOR_TRANSPORT_ERROR;
}

/**
* @fn		sm331_internal_cmd_with_status
* @brief	transport SCSI Command from this driver.
* @param	srb	    : SCSI command structure
* @param	us_data	    : USB Storage device structure
* @param	cmnd	    : Command Description
* @param	cmnd_length : Length of Command Description
* @param	reply_buf   : Reply buffer
* @param	reply_len   : Length of reply buffer
* @param	direction   : DMA_BIDIRECTIONAL, DMA_TO_DEVICE, DMA_FROM_DEVICE, DMA_NONE
* @param	status      : Error Status
* @return	success : 0	failure : not zero
* @date		2008/07/10
*/
int sm331_internal_cmd_with_status(
	struct scsi_cmnd *srb, void *us_data, __u8 *cmnd, __u8 cmnd_length,
	void *reply_buf, unsigned int reply_len, int direction, int *status)
{
	int ret;
	struct us_data *us = (struct us_data *)us_data;

	ret = __sm331_internal_cmd(srb, us_data, cmnd, cmnd_length, reply_buf, reply_len, direction);

	*status = ret;
	if((cmnd[0] != REQUEST_SENSE)
			&& !(cmnd[0] == 0xF0 && cmnd[1] == 0x2C)){
		if(set_func && request_sense && us){
			request_sense(srb, us->extra, status);
		}
	}

	return ret;
}

EXPORT_SYMBOL(sm331_internal_cmd_with_status);

/**
* @fn		sm331_internal_cmd
* @brief	transport SCSI Command from this driver.
* @param	srb	    : SCSI command structure
* @param	us_data	    : USB Storage device structure
* @param	cmnd	    : Command Description
* @param	cmnd_length : Length of Command Description
* @param	reply_buf   : Reply buffer
* @param	reply_len   : Length of reply buffer
* @param	direction   : DMA_BIDIRECTIONAL, DMA_TO_DEVICE, DMA_FROM_DEVICE, DMA_NONE
* @return	success : 0	failure : not zero
* @date		2008/07/10
*/
int sm331_internal_cmd(
	struct scsi_cmnd *srb, void *us_data, __u8 *cmnd, __u8 cmnd_length,
	void *reply_buf, unsigned int reply_len, int direction)
{
	int status = 0;

	return sm331_internal_cmd_with_status(
			srb, us_data, cmnd, cmnd_length, reply_buf, reply_len, direction, &status);
}

EXPORT_SYMBOL(sm331_internal_cmd);

/**
* @fn		sm331_read_transport
* @brief	transport READ SCSI Command (READ 6, 10, 12, 16)
* @param	srb : SCSI command structure
* @param	us  : USB Storage device structure
* @return	success : 0	failure : not zero
* @date		2008/07/10
*/
static int sm331_read_transport(struct scsi_cmnd *srb, struct us_data *us)
{
	int ret;
	int retry = 0;
	int reset_done = 0;

	if(likely(set_func && (read_transport != NULL))){
		ret = read_transport(srb, us->extra, read_retry);
		if(ret == SM331_SDOP_FLAG){
			return 0;
		}
		else if(ret){
			return ret;
		}
	}

	ret = -EIO;
	while(ret && retry <= read_retry){
		int status = SM331_NO_ERROR;

		if(retry)
			printk("sm331_read : retry %d times\n", retry);
		ret = sm331_normal_transport_with_status(srb, us, &status);
		retry++;

		if(ret){
			if(status == SM331_MEDIA_CHANGED){
				printk("sm331_read : media changed\n");
				break;
			}
		}

		if (reset_done) continue;
		if((ret == SM331_TRANS_TIMEOUT)
				|| (ret && last_retry_reset && (retry == read_retry))){
			if(set_func && reset_device && read_enable_reset){
				reset_done = 1;
				printk("%s : try to reset ...\n", __FUNCTION__);
				if(reset_device(srb, us->extra))
					break;
			}
		}
	}

	if(ret){
		if(likely(set_func && (notify_error != NULL))){
			notify_error(us->extra, (ret > 0 ? -EIO : ret), 0);
		}

		printk("sm331_read failed : retry %d times\n", retry - 1);
		if(ret < 0){
			ret = USB_STOR_TRANSPORT_FAILED;
		}
	}

	return ret;
}

/**
* @fn		sm331_alignment_write
* @brief	transport alignment WRITE
* @param	srb : SCSI command structure
* @param	us  : USB Storage device structure
* @return	success : 0	failure : not zero
* @date		2009/01/14
*/
static int sm331_alignment_write(struct scsi_cmnd *srb, struct us_data *us)
{
	int ret;

	if(likely(set_func && (alignment_write != NULL))){
		ret = alignment_write(srb, us->extra);

		if(ret == SM331_SDOP_FLAG){ //Not need normal write.
			return 0;
		}
		else if(ret){
			return ret;
		}
	}

	return sm331_normal_transport(srb, us);
}

/**
* @fn		sm331_write_transport
* @brief	transport WRITE SCSI Command (WRITE 6, 10, 12, 16)
* @param	srb : SCSI command structure
* @param	us  : USB Storage device structure
* @return	success : 0	failure : not zero
* @date		2008/07/10
*/
static int sm331_write_transport(struct scsi_cmnd *srb, struct us_data *us)
{
	int ret;
	int retry = 0;
	int reset_done = 0;

	if(likely(set_func && (write_transport != NULL))){
		ret = write_transport(srb, us->extra, write_retry);
		if(ret == SM331_SDOP_FLAG){ //Not need normal write.
			return 0;
		}
		else if(ret){
			return ret;
		}
	}

	ret = -EIO;
	while(ret && retry <= write_retry){
		if(retry){
			printk("sm331_write : retry %d times\n", retry);
			if(likely(set_func && (retry_write != NULL))){
				ret = retry_write(srb, us->extra);
				if(ret == SM331_SDOP_FLAG)
					ret = sm331_alignment_write(srb, us);
			}
			else{
				ret = sm331_alignment_write(srb, us);
			}
		}
		else{
			ret = sm331_alignment_write(srb, us);
		}
		retry++;

		if (reset_done) continue;
		if((ret == SM331_TRANS_TIMEOUT)
				|| (ret && last_retry_reset && (retry == write_retry))){
			if(set_func && reset_device && write_enable_reset){
				reset_done = 1;
				printk("%s : try to reset ...\n", __FUNCTION__);
				if(reset_device(srb, us->extra))
					break;
			}
		}
	}

	if(ret){
		printk("sm331_write failed : retry %d times\n", retry - 1);
		if(likely(set_func && (notify_error != NULL))){
			notify_error(us->extra, (ret > 0 ? -EIO : ret), 1);
		}

		if(ret < 0){
			ret = USB_STOR_TRANSPORT_FAILED;
		}
	}

	return ret;
}

/**
* @fn		sm331_other_transport
* @brief	transport OTHER SCSI Command (READ 6, 10, 12, 16)
* @param	srb : SCSI command structure
* @param	us  : USB Storage device structure
* @return	success : 0	failure : not zero
* @date		2008/07/10
*/
static int sm331_other_transport(struct scsi_cmnd *srb, struct us_data *us)
{
	int ret;
	if(likely(set_func && (other_transport != NULL))){
		ret = other_transport(srb, us->extra);
		if(ret == 0){ //成功したらなにもせずぬける
			return ret;
		}
	}

	return sm331_normal_transport(srb, us);
}

/**
* @fn		sm331_transport
* @brief	interface of transport SCSI Command.
* @param	srb : SCSI command structure
* @param	us  : USB Storage device structure
* @return	success : 0	failure : not zero
* @date		2008/07/10
*/
int sm331_transport(struct scsi_cmnd *srb, struct us_data *us)
{
	int ret = -EIO;
	unsigned char key;

	u8 slot = us->pusb_dev->portnum - CONFIG_SM331_AV_SLOT_OFFSET;

	if(!is_av_slot(us->pusb_dev))
		return sm331_normal_transport(srb, us);
/*
	if(!srb->device->minimum_timeout)
		srb->device->minimum_timeout = SD_MINIMUM_TIMEOUT;
*/
	if(!us->extra){
		if(likely(set_func && (alloc_card_info != NULL))){
			ret = alloc_card_info(
				us, &us->extra, us->pusb_dev, us_to_host(us), &us->dev_mutex,
				&us->fflags, US_FLIDX_RESETTING, US_FLIDX_ABORTING, slot
			);
			if(ret){
				return ret;
			};

			us->extra_destructor = __sm331_card_info_destructor;
		}
	}

	if(!srb->device->notify_ops){
		srb->device->extra = us;
		srb->device->notify_ops = &scsi_ops;
	}

	if(!srb->device->use_10_for_rw){
		srb->device->use_10_for_rw = 1;
	}
/*
	if(srb->device->notify_mount){
		srb->device->notify_mount = 0;
		sm331_card_info_destructor(us->extra, 1);
	}
*/
	switch(srb->cmnd[0]){
	case WRITE_6:
	case WRITE_10:
	case WRITE_12:
		ret = sm331_write_transport(srb, us);
		break;

	case WRITE_16:
		printk("WRITE_16 is not support\n");
		return sm331_normal_transport(srb, us);
	case WRITE_LONG:
		printk("WRITE_LONG is not support\n");
		return sm331_normal_transport(srb, us);

	case READ_6:
	case READ_10:
	case READ_12:
		ret = sm331_read_transport(srb, us);
		break;

	case READ_16:
		printk("READ_16 is not support\n");
		return sm331_normal_transport(srb, us);
	case READ_LONG:
		printk("READ_LONG is not support\n");
		return sm331_normal_transport(srb, us);

	default:
		ret =  sm331_other_transport(srb, us);
	}

	if(ret == SM331_SDOP_FSERR){
		memcpy(srb->sense_buffer, usb_stor_sense_invalidCDB, sizeof(usb_stor_sense_invalidCDB));
		ret = USB_STOR_XFER_ERROR;
	}

	key = srb->sense_buffer[2] & 0xF;
	if(key){
		if(key == NOT_READY || key == UNIT_ATTENTION)
			sm331_card_info_destructor(us->extra, 0);
	}

	return ret;
}

/**
* @fn		sm331_mount_fs
* @brief	Notified mounted device
* @param	sdev  : SCSI device structure
* @return	success : 0	failure : not zero
* @date		2009/09/28
*/
int sm331_mount_fs(struct scsi_device *sdev)
{
	struct us_data *us = (struct us_data *)sdev->extra;

	if(!us)
		return -EINVAL;

	if(!is_av_slot(us->pusb_dev))
		return 0;

	if(likely(set_func && (notify_mount != NULL))){
		notify_mount(us->extra);
	}

	return 0;
}

/**
* @fn		sm331_umount_fs
* @brief	Notified unmounted device
* @param	sdev  : SCSI device structure
* @return	success : 0	failure : not zero
* @date		2009/09/28
*/
int sm331_umount_fs(struct scsi_device *sdev)
{
	struct us_data *us = (struct us_data *)sdev->extra;

	if(!us)
		return -EINVAL;

	if(!is_av_slot(us->pusb_dev))
		return 0;

	if(likely(set_func && (notify_umount != NULL))){
		notify_umount(us->extra);
	}

	return 0;
}

/**
* @fn		usb_stor_sm331_init
* @brief	initialize of this driver
* @param	us  : USB Storage device structure
* @return	success : 0	failure : not zero
* @date		2008/07/10
*/
int usb_stor_sm331_init (struct us_data *us)
{
	struct Scsi_Host *scsi_host = us_to_host(us);

	if(is_av_slot(us->pusb_dev)){
		if(scsi_host){
			scsi_host->max_sectors = 256;
		}
	}

	return USB_STOR_TRANSPORT_GOOD;
}

/**
* @fn		usb_stor_sm331_reset
* @brief	reset this driver
* @param	us  : USB Storage device structure
* @return	success : 0	failure : not zero
* @date		2008/07/10
*/
int usb_stor_sm331_reset(struct us_data *us)
{
	/* We don't really have this feature. */
	return FAILED;
}
