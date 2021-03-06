///////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2000-2003 Intel Corporation 
// All rights reserved. 
//
// Redistribution and use in source and binary forms, with or without 
// modification, are permitted provided that the following conditions are met: 
//
// * Redistributions of source code must retain the above copyright notice, 
// this list of conditions and the following disclaimer. 
// * Redistributions in binary form must reproduce the above copyright notice, 
// this list of conditions and the following disclaimer in the documentation 
// and/or other materials provided with the distribution. 
// * Neither name of Intel Corporation nor the names of its contributors 
// may be used to endorse or promote products derived from this software 
// without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR 
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL INTEL OR 
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR 
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY 
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
///////////////////////////////////////////////////////////////////////////

#include "upnp_tv_ctrlpt.h"

/*
   Mutex for protecting the global device list
   in a multi-threaded, asynchronous environment.
   All functions should lock this mutex before reading
   or writing the device list. 
 */
ithread_mutex_t DeviceListMutex;

UpnpClient_Handle ctrlpt_handle = -1;

char TvDeviceType[] = "urn:schemas-upnp-org:device:MediaServer:1";
char *TvServiceName[] = { "Control", "Picture" };

/*
   Global arrays for storing variable names and counts for 
   TvControl and TvPicture services 
 */
char *TvVarName[TV_SERVICE_SERVCOUNT][TV_MAXVARS] = {
    {"Power", "Channel", "Volume", ""},
    {"Color", "Tint", "Contrast", "Brightness"}
};
char TvVarCount[TV_SERVICE_SERVCOUNT] =
    { TV_CONTROL_VARCOUNT, TV_PICTURE_VARCOUNT };

/*
   Timeout to request during subscriptions 
 */
int default_timeout = 1801;

/*
   The first node in the global device list, or NULL if empty 
 */
struct TvDeviceNode *GlobalDeviceList = NULL;

static struct TvDevice *CtrlPointSearchDeviceListByUDN(char *UDN)
{
	struct TvDeviceNode *deviceNode = GlobalDeviceList;

	while (deviceNode) {
		if( strcmp( deviceNode->device.UDN, UDN ) == 0 )
			return (&deviceNode->device);
		deviceNode = deviceNode->next;
	}
	return NULL;
}

static void CtrlPointAddDeviceList(struct TvDeviceNode *deviceNode)
{
	struct TvDeviceNode *tmpdevnode = GlobalDeviceList;

	deviceNode->next = NULL;

	// Insert the new device node in the list
	if (tmpdevnode) {
		while (tmpdevnode) {
			if (tmpdevnode->next) {
				tmpdevnode = tmpdevnode->next;
			} else {
				tmpdevnode->next = deviceNode;
				break;
			}
		}
	} else {
		GlobalDeviceList = deviceNode;
	}
}

static int CtrlPointFindAndParseService(
			IXML_Document *DescDoc, char *location, struct TvDevice *tvdevice)
{
	int i;
	int ret;
	char *serviceType = NULL;
	char *serviceId = NULL;
	char *baseURL = NULL;
	char *base;
	char *relcontrolURL = NULL, *releventURL = NULL;
	char *controlURL = NULL, *eventURL = NULL;
	char *SCPDURL = NULL;
	IXML_NodeList *serviceList = NULL;
	IXML_Element *service = NULL;

	baseURL = SampleUtil_GetFirstDocumentItem(DescDoc, "URLBase");
	if (baseURL)
		base = baseURL;
	else
	base = location;

	serviceList = SampleUtil_GetFirstServiceList(DescDoc);
	tvdevice->tv_service_num = ixmlNodeList_length(serviceList);
	for (i=0;i<tvdevice->tv_service_num;i++) {
		struct tv_service *tvservice;
		tvservice = &tvdevice->TvService[i];
		tvservice->TvServiceState = 0;
        service = (IXML_Element *)ixmlNodeList_item(serviceList, i);

		serviceType = SampleUtil_GetFirstElementItem((IXML_Element *)service, "serviceType");
		strcpy(tvservice->ServiceType, serviceType);

		serviceId = SampleUtil_GetFirstElementItem(service, "serviceId");
		strcpy(tvservice->ServiceId, serviceId);

		SCPDURL = SampleUtil_GetFirstElementItem(service, "SCPDURL");
		strcpy(tvservice->SCPDURL, SCPDURL);

		relcontrolURL = SampleUtil_GetFirstElementItem(service, "controlURL");
		releventURL = SampleUtil_GetFirstElementItem(service, "eventSubURL");

		controlURL = malloc(strlen(base) + strlen(relcontrolURL) + 1);
		if (controlURL) {
			ret = UpnpResolveURL(base, relcontrolURL, controlURL);
			if (ret != UPNP_E_SUCCESS)
				SampleUtil_Print("Error generating controlURL from %s + %s\n",
								base, relcontrolURL );
		}
		strcpy(tvservice->ControlURL, controlURL);

		eventURL = malloc(strlen(base) + strlen(releventURL) + 1);
		if (eventURL) {
			ret = UpnpResolveURL(base, releventURL, eventURL);
			if (ret != UPNP_E_SUCCESS)
				SampleUtil_Print("Error generating eventURL from %s + %s\n",
								base, releventURL);
		}
		strcpy(tvservice->EventURL, eventURL);

		if (relcontrolURL)
			free(relcontrolURL);
		if (controlURL)
			free(controlURL);
		if (releventURL)
			free(releventURL);
		if (eventURL)
			free(eventURL);
		if (SCPDURL)
			free(SCPDURL);
		relcontrolURL = releventURL = NULL;
		controlURL = eventURL = NULL;
		SCPDURL = NULL;
		if (serviceType)
			free(serviceType);
		serviceType = NULL;
		if (serviceId)
			free(serviceId);
		serviceId = NULL;
	}

	if (serviceList)
		ixmlNodeList_free(serviceList);
	if (baseURL)
		free(baseURL);

	return (tvdevice->tv_service_num);
}


/********************************************************************************
 * TvCtrlPointDeleteNode
 *
 * Description: 
 *       Delete a device node from the global device list.  Note that this
 *       function is NOT thread safe, and should be called from another
 *       function that has already locked the global device list.
 *
 * Parameters:
 *   node -- The device node
 *
 ********************************************************************************/
int
TvCtrlPointDeleteNode( struct TvDeviceNode *node )
{
    int rc,
      service,
      var;

    if( NULL == node ) {
        SampleUtil_Print( "ERROR: TvCtrlPointDeleteNode: Node is empty" );
        return TV_ERROR;
    }

	if( node->device.DescDoc )
		ixmlDocument_free(node->device.DescDoc);

    for( service = 0; service < TV_SERVICE_SERVCOUNT; service++ ) {
        /*
           If we have a valid control SID, then unsubscribe 
         */
        if( strcmp( node->device.TvService[service].SID, "" ) != 0 ) {
            rc = UpnpUnSubscribe( ctrlpt_handle,
                                  node->device.TvService[service].SID );
            if( UPNP_E_SUCCESS == rc ) {
                SampleUtil_Print
                    ( "Unsubscribed from Tv %s EventURL with SID=%s",
                      TvServiceName[service],
                      node->device.TvService[service].SID );
            } else {
                SampleUtil_Print
                    ( "Error unsubscribing to Tv %s EventURL -- %d",
                      TvServiceName[service], rc );
            }
        }

        for( var = 0; var < TvVarCount[service]; var++ ) {
            if( node->device.TvService[service].VariableStrVal[var] ) {
                free( node->device.TvService[service].
                      VariableStrVal[var] );
            }
        }
    }

    //Notify New Device Added
    SampleUtil_StateUpdate( NULL, NULL, node->device.UDN, DEVICE_REMOVED );
    free( node );
    node = NULL;

    return TV_SUCCESS;
}

/********************************************************************************
 * TvCtrlPointRemoveDevice
 *
 * Description: 
 *       Remove a device from the global device list.
 *
 * Parameters:
 *   UDN -- The Unique Device Name for the device to remove
 *
 ********************************************************************************/
int
TvCtrlPointRemoveDevice( char *UDN )
{
    struct TvDeviceNode *curdevnode,
     *prevdevnode;

    ithread_mutex_lock( &DeviceListMutex );

    curdevnode = GlobalDeviceList;
    if( !curdevnode ) {
        SampleUtil_Print
            ( "WARNING: TvCtrlPointRemoveDevice: Device list empty" );
    } else {
        if( 0 == strcmp( curdevnode->device.UDN, UDN ) ) {
            GlobalDeviceList = curdevnode->next;
            TvCtrlPointDeleteNode( curdevnode );
        } else {
            prevdevnode = curdevnode;
            curdevnode = curdevnode->next;

            while( curdevnode ) {
                if( strcmp( curdevnode->device.UDN, UDN ) == 0 ) {
                    prevdevnode->next = curdevnode->next;
                    TvCtrlPointDeleteNode( curdevnode );
                    break;
                }

                prevdevnode = curdevnode;
                curdevnode = curdevnode->next;
            }
        }
    }

    ithread_mutex_unlock( &DeviceListMutex );

    return TV_SUCCESS;
}

/********************************************************************************
 * TvCtrlPointRemoveAll
 *
 * Description: 
 *       Remove all devices from the global device list.
 *
 * Parameters:
 *   None
 *
 ********************************************************************************/
int
TvCtrlPointRemoveAll( void )
{
    struct TvDeviceNode *curdevnode,
     *next;

    ithread_mutex_lock( &DeviceListMutex );

    curdevnode = GlobalDeviceList;
    GlobalDeviceList = NULL;

    while( curdevnode ) {
        next = curdevnode->next;
        TvCtrlPointDeleteNode( curdevnode );
        curdevnode = next;
    }

    ithread_mutex_unlock( &DeviceListMutex );

    return TV_SUCCESS;
}

/********************************************************************************
 * TvCtrlPointRefresh
 *
 * Description: 
 *       Clear the current global device list and issue new search
 *	 requests to build it up again from scratch.
 *
 * Parameters:
 *   None
 *
 ********************************************************************************/
int
TvCtrlPointRefresh( void )
{
    int rc;

    TvCtrlPointRemoveAll();

    /*
       Search for all devices of type tvdevice version 1, 
       waiting for up to 5 seconds for the response 
     */
    rc = UpnpSearchAsync( ctrlpt_handle, 5, TvDeviceType, NULL );
    if( UPNP_E_SUCCESS != rc ) {
        SampleUtil_Print( "Error sending search request%d", rc );
        return TV_ERROR;
    }

    return TV_SUCCESS;
}

/********************************************************************************
 * TvCtrlPointGetVar
 *
 * Description: 
 *       Send a GetVar request to the specified service of a device.
 *
 * Parameters:
 *   service -- The service
 *   devnum -- The number of the device (order in the list,
 *             starting with 1)
 *   varname -- The name of the variable to request.
 *
 ********************************************************************************/
int
TvCtrlPointGetVar( int service,
                   int devnum,
                   char *varname )
{
    struct TvDeviceNode *devnode;
    int rc;

    ithread_mutex_lock( &DeviceListMutex );

    rc = TvCtrlPointGetDevice( devnum, &devnode );

    if( TV_SUCCESS == rc ) {
        rc = UpnpGetServiceVarStatusAsync( ctrlpt_handle,
                                           devnode->device.
                                           TvService[service].ControlURL,
                                           varname,
                                           TvCtrlPointCallbackEventHandler,
                                           NULL );
        if( rc != UPNP_E_SUCCESS ) {
            SampleUtil_Print
                ( "Error in UpnpGetServiceVarStatusAsync -- %d", rc );
            rc = TV_ERROR;
        }
    }

    ithread_mutex_unlock( &DeviceListMutex );

    return rc;
}

int
TvCtrlPointGetPower( int devnum )
{
    return TvCtrlPointGetVar( TV_SERVICE_CONTROL, devnum, "Power" );
}

/* A sample for ushare "Browse" service action.
   in:
   actionname = "Browse"
   char *param_name[];
   char *param_val[];
   param_name[0] = "ObjectID"
   param_val[0] = "0"
   param_name[1] = "BrowseFlag"
   param_val[1] = "BrowseDirectChildren"
   param_name[2] = "Filter"
   param_val[2] = "*"
   param_name[3] = "StartingIndex"
   param_val[3] = "0"
   param_name[4] = "RequestedCount"
   param_val[4] = "0"
   param_name[5] = "SortCriteria"
   param_val[5] = ""
   param_count = 5
   out:
   print by ixmlPrintDocument
*/
IXML_Document *CtrlPointSendAction(char *UDN, int service, char *actionname,
                        char **param_name, char **param_val, int param_count)
{
    struct TvDevice *dev;
	struct tv_service *tvservice;
    IXML_Document *ActionXML = NULL;
	IXML_Document *ActionRespXML = NULL;
    int ret = -1;
    int i;

    ithread_mutex_lock(&DeviceListMutex);

    dev = CtrlPointSearchDeviceListByUDN(UDN);
    if (dev) {
		tvservice = &dev->TvService[service];
        if (param_count == 0) {
            ActionXML = UpnpMakeAction(actionname, tvservice->ServiceType, 0, NULL);
        } else {
            for (i=0;i<param_count;i++) {
                if (UpnpAddToAction(&ActionXML, actionname, tvservice->ServiceType,
                      param_name[i], param_val[i] ) != UPNP_E_SUCCESS) {
                    SampleUtil_Print
                        ( "ERROR: TvCtrlPointSendAction: Trying to add action param" );
                    //return -1; // TBD - BAD! leaves mutex locked
                }
            }
        }

        ret = UpnpSendAction(ctrlpt_handle,
			                tvservice->ControlURL, tvservice->ServiceType,
                            NULL, ActionXML, &ActionRespXML);

        if (ret != UPNP_E_SUCCESS) {
            SampleUtil_Print( "Error in UpnpSendActionAsync -- %d", ret );
            ret = -1;
        }
    }

    ithread_mutex_unlock( &DeviceListMutex );

    if(ActionXML)
        ixmlDocument_free(ActionXML);

    return ActionRespXML;
}

/********************************************************************************
 * TvCtrlPointSendAction
 *
 * Description: 
 *       Send an Action request to the specified service of a device.
 *
 * Parameters:
 *   service -- The service
 *   devnum -- The number of the device (order in the list,
 *             starting with 1)
 *   actionname -- The name of the action.
 *   param_name -- An array of parameter names
 *   param_val -- The corresponding parameter values
 *   param_count -- The number of parameters
 *
 ********************************************************************************/
int
TvCtrlPointSendAction( int service,
                       int devnum,
                       char *actionname,
                       char **param_name,
                       char **param_val,
                       int param_count )
{
    struct TvDeviceNode *devnode;
	struct tv_service *tvservice;
    IXML_Document *actionNode = NULL;
    int rc = TV_SUCCESS;
    int param;

    ithread_mutex_lock( &DeviceListMutex );

    rc = TvCtrlPointGetDevice( devnum, &devnode );
    if( TV_SUCCESS == rc ) {
		tvservice = &devnode->device.TvService[service];
        if( 0 == param_count ) {
            actionNode =
                UpnpMakeAction(actionname, tvservice->ServiceType, 0, NULL);
        } else {
            for( param = 0; param < param_count; param++ ) {
                if( UpnpAddToAction( &actionNode, actionname,
                      tvservice->ServiceType,
                      param_name[param], param_val[param] ) != UPNP_E_SUCCESS ) {
                    SampleUtil_Print
                        ( "ERROR: TvCtrlPointSendAction: Trying to add action param" );
                    //return -1; // TBD - BAD! leaves mutex locked
                }
            }
        }

        rc = UpnpSendActionAsync( ctrlpt_handle,
                                  tvservice->ControlURL, tvservice->ServiceType,
                                  NULL, actionNode,
                                  TvCtrlPointCallbackEventHandler, NULL );

        if( rc != UPNP_E_SUCCESS ) {
            SampleUtil_Print( "Error in UpnpSendActionAsync -- %d", rc );
            rc = TV_ERROR;
        }
    }

    ithread_mutex_unlock( &DeviceListMutex );

    if( actionNode )
        ixmlDocument_free( actionNode );

    return rc;
}

int TvCtrlPointSendPowerOn(int devnum)
{
    return TvCtrlPointSendAction( TV_SERVICE_CONTROL, devnum, "PowerOn",
                                  NULL, NULL, 0 );
}

int TvCtrlPointSendSetChannel(int devnum, int channel)
{
#define PNUM 1
	char *param_name[PNUM];
	char *param_val[PNUM];
	char param_val_tmp[50];

	param_name[0] = "Channel";
	sprintf(param_val_tmp, "%d", channel);
	param_val[0] = param_val_tmp;

	return TvCtrlPointSendAction(TV_SERVICE_CONTROL, devnum, "SetChannel",
								&param_name[0], &param_val[0], PNUM);
}

/********************************************************************************
 * TvCtrlPointGetDevice
 *
 * Description: 
 *       Given a list number, returns the pointer to the device
 *       node at that position in the global device list.  Note
 *       that this function is not thread safe.  It must be called 
 *       from a function that has locked the global device list.
 *
 * Parameters:
 *   devnum -- The number of the device (order in the list,
 *             starting with 1)
 *   devnode -- The output device node pointer
 *
 ********************************************************************************/
int
TvCtrlPointGetDevice( int devnum,
                      struct TvDeviceNode **devnode )
{
    int count = devnum;
    struct TvDeviceNode *tmpdevnode = NULL;

    if( count )
        tmpdevnode = GlobalDeviceList;

    while( --count && tmpdevnode ) {
        tmpdevnode = tmpdevnode->next;
    }

    if( !tmpdevnode ) {
        SampleUtil_Print( "Error finding TvDevice number -- %d", devnum );
        return TV_ERROR;
    }

    *devnode = tmpdevnode;
    return TV_SUCCESS;
}

/********************************************************************************
 * TvCtrlPointPrintList
 *
 * Description: 
 *       Print the universal device names for each device in the global device list
 *
 * Parameters:
 *   None
 *
 ********************************************************************************/
int
TvCtrlPointPrintList()
{
    struct TvDeviceNode *tmpdevnode;
    int i = 0;

    ithread_mutex_lock( &DeviceListMutex );

    printf("TvCtrlPointPrintList:\n");
    tmpdevnode = GlobalDeviceList;
    while( tmpdevnode ) {
        printf(" %3d -- %s\n", ++i, tmpdevnode->device.UDN);
        tmpdevnode = tmpdevnode->next;
    }
    printf("\n");
    ithread_mutex_unlock( &DeviceListMutex );

    return TV_SUCCESS;
}

/********************************************************************************
 * TvCtrlPointPrintDevice
 *
 * Description: 
 *       Print the identifiers and state table for a device from
 *       the global device list.
 *
 * Parameters:
 *   devnum -- The number of the device (order in the list,
 *             starting with 1)
 *
 ********************************************************************************/
int
TvCtrlPointPrintDevice( int devnum )
{
    struct TvDeviceNode *tmpdevnode;
    int i = 0,
      service,
      var;
    char spacer[15];

    if( devnum <= 0 ) {
        SampleUtil_Print
            ( "Error in TvCtrlPointPrintDevice: invalid devnum = %d",
              devnum );
        return TV_ERROR;
    }

    ithread_mutex_lock( &DeviceListMutex );

    SampleUtil_Print( "TvCtrlPointPrintDevice:" );
    tmpdevnode = GlobalDeviceList;
    while( tmpdevnode ) {
        i++;
        if( i == devnum )
            break;
        tmpdevnode = tmpdevnode->next;
    }

    if( !tmpdevnode ) {
        SampleUtil_Print
            ( "Error in TvCtrlPointPrintDevice: invalid devnum = %d  --  actual device count = %d",
              devnum, i );
    } else {
        SampleUtil_Print( "  TvDevice -- %d", devnum );
        SampleUtil_Print( "    |                  " );
        SampleUtil_Print( "    +- UDN        = %s",
                          tmpdevnode->device.UDN );
        SampleUtil_Print( "    +- DescDocURL     = %s",
                          tmpdevnode->device.DescDocURL );
        SampleUtil_Print( "    +- DeviceType   = %s",
                          tmpdevnode->device.DeviceType);
        SampleUtil_Print( "    +- FriendlyName   = %s",
                          tmpdevnode->device.FriendlyName );
        SampleUtil_Print( "    +- PresURL        = %s",
                          tmpdevnode->device.PresURL );
        SampleUtil_Print( "    +- Adver. TimeOut = %d",
                          tmpdevnode->device.AdvrTimeOut );

        for (service=0;service<tmpdevnode->device.tv_service_num;service++) {
            if( service < TV_SERVICE_SERVCOUNT - 1 )
                sprintf( spacer, "    |    " );
            else
                sprintf( spacer, "         " );
            SampleUtil_Print( "    |                  " );
            SampleUtil_Print( "    +- Service %d", service);
            SampleUtil_Print( "%s+- ServiceId       = %s", spacer,
                              tmpdevnode->device.TvService[service].
                              ServiceId );
            SampleUtil_Print( "%s+- ServiceType     = %s", spacer,
                              tmpdevnode->device.TvService[service].
                              ServiceType );
            SampleUtil_Print( "%s+- EventURL        = %s", spacer,
                              tmpdevnode->device.TvService[service].
                              EventURL );
            SampleUtil_Print( "%s+- ControlURL      = %s", spacer,
                              tmpdevnode->device.TvService[service].
                              ControlURL );
            SampleUtil_Print( "%s+- SCPDURL         = %s", spacer,
                              tmpdevnode->device.TvService[service].
                              SCPDURL );
            SampleUtil_Print( "%s+- SID             = %s", spacer,
                              tmpdevnode->device.TvService[service].SID );
            SampleUtil_Print( "%s+- ServiceStateTable", spacer );
/*
            for( var = 0; var < TvVarCount[service]; var++ ) {
                SampleUtil_Print( "%s     +- %-10s = %s", spacer,
                                  TvVarName[service][var],
                                  tmpdevnode->device.TvService[service].
                                  VariableStrVal[var] );
            }
*/
        }
    }

    SampleUtil_Print( "" );
    ithread_mutex_unlock( &DeviceListMutex );

    return TV_SUCCESS;
}

int CtrlPointAddService(char *UDN, int service)
{
    struct TvDevice *device;
    char *serviceId = NULL;
	char *serviceType = NULL;
    char *eventURL = NULL;
    char *controlURL = NULL;
    Upnp_SID eventSID;
    int TimeOut = default_timeout;
	struct tv_service *tvservice;
	int ret = -1;

    ithread_mutex_lock( &DeviceListMutex );

	/* search device by UDN */
	device = CtrlPointSearchDeviceListByUDN(UDN);
	if (!device) {
		SampleUtil_Print("Error subscribe: No found device(%s)", UDN);
		goto addexit;
	}

	if (service >= device->tv_service_num) {
		SampleUtil_Print("Error subscribe: service exceed(%d)", service);
		goto addexit;
	}

	tvservice = &device->TvService[service];
	if (tvservice->TvServiceState) {
		SampleUtil_Print("Error subscribe: service already subscribed");
		goto addexit;
	}

	/* subscribe one service */
	ret = UpnpSubscribe(ctrlpt_handle, tvservice->EventURL, &TimeOut, eventSID);
	if (ret == UPNP_E_SUCCESS) {
		SampleUtil_Print("Subscribed to EventURL with SID=%s", eventSID);
	} else {
		SampleUtil_Print("Error Subscribing to EventURL -- %d", ret);
		strcpy(eventSID, "");
		goto addexit;
	}
	strcpy(tvservice->SID, eventSID);
	tvservice->TvServiceState = 1;

addexit:
    ithread_mutex_unlock(&DeviceListMutex);
}

int CtrlPointRemoveService(char *UDN, int service)
{
    struct TvDevice *device;
	struct tv_service *tvservice;
	int ret = -1;

    ithread_mutex_lock( &DeviceListMutex );

	/* search device by UDN */
	device = CtrlPointSearchDeviceListByUDN(UDN);
	if (!device) {
		SampleUtil_Print("Error unsubscribe: No found device(%s)", UDN);
		goto removeexit;
	}

	if (service >= device->tv_service_num) {
		SampleUtil_Print("Error unsubscribe: service exceed(%d)", service);
		goto removeexit;
	}

	tvservice = &device->TvService[service];
	if (!tvservice->TvServiceState) {
		SampleUtil_Print("Error unsubscribe: service not subscribed");
		goto removeexit;
	}

	/* subscribe one service */
	ret = UpnpUnSubscribe(ctrlpt_handle, tvservice->SID);
	if (ret == UPNP_E_SUCCESS) {
		SampleUtil_Print("unsubscribed");
	} else {
		SampleUtil_Print("Error unsubscribing");
		goto removeexit;
	}
	strcpy(tvservice->SID, "");
	tvservice->TvServiceState = 0;

removeexit:
    ithread_mutex_unlock(&DeviceListMutex);
}

/********************************************************************************
 * TvCtrlPointAddDevice
 *
 * Description: 
 *       If the device is not already included in the global device list,
 *       add it.  Otherwise, update its advertisement expiration timeout.
 *
 * Parameters:
 *   DescDoc -- The description document for the device
 *   location -- The location of the description document URL
 *   expires -- The expiration time for this advertisement
 *
 ********************************************************************************/
struct TvDeviceNode *TvCtrlPointAddDevice(struct Upnp_Discovery *d_event,
							IXML_Document *DescDoc, char *UDN)
{
    char *deviceType = NULL;
    char *friendlyName = NULL;
    char presURL[200];
    char *baseURL = NULL;
    char *relURL = NULL;
    struct TvDeviceNode *deviceNode = NULL;
    struct TvDeviceNode *tmpdevnode;
	struct TvDevice *tvdevice;
    int ret = 1;
    int found = 0;
    int service,
      var;

	/* Read key elements from description document */
	baseURL = SampleUtil_GetFirstDocumentItem(DescDoc, "URLBase");
	relURL = SampleUtil_GetFirstDocumentItem(DescDoc, "presentationURL");

	ret = UpnpResolveURL((baseURL?baseURL:d_event->Location), relURL, presURL);
    if (ret != UPNP_E_SUCCESS) {
		printf("Error generating presURL from %s + %s\n", baseURL, relURL);
		goto error1;
	}

	/* Create a new device node and device structure */
	deviceNode = (struct TvDeviceNode *)malloc(sizeof(struct TvDeviceNode));
	if (!deviceNode) {
		printf("error TvCtrlPointAddDevice memory over\n");
		goto error1;
	}

	deviceType = SampleUtil_GetFirstDocumentItem(DescDoc, "deviceType");
	friendlyName = SampleUtil_GetFirstDocumentItem(DescDoc, "friendlyName");

	tvdevice = &deviceNode->device;
	strcpy(tvdevice->UDN, UDN);
	strcpy(tvdevice->DescDocURL, d_event->Location);
	strcpy(tvdevice->DeviceType, deviceType);
	strcpy(tvdevice->FriendlyName, friendlyName);
	strcpy(tvdevice->PresURL, presURL);
	tvdevice->AdvrTimeOut = d_event->Expires;
	tvdevice->DescDoc = DescDoc;

	/* init service structure */
	CtrlPointFindAndParseService(DescDoc, d_event->Location, tvdevice);

	//Notify New Device Added
	SampleUtil_StateUpdate(NULL, NULL, deviceNode->device.UDN, DEVICE_ADDED);

	if (deviceType)
		free(deviceType);
	if (friendlyName)
		free(friendlyName);
error1:
	if (baseURL)
		free(baseURL);
	if (relURL)
		free(relURL);

	return deviceNode;
}

/********************************************************************************
 * TvStateUpdate
 *
 * Description: 
 *       Update a Tv state table.  Called when an event is
 *       received.  Note: this function is NOT thread save.  It must be
 *       called from another function that has locked the global device list.
 *
 * Parameters:
 *   UDN     -- The UDN of the parent device.
 *   Service -- The service state table to update
 *   ChangedVariables -- DOM document representing the XML received
 *                       with the event
 *   State -- pointer to the state table for the Tv  service
 *            to update
 *
 ********************************************************************************/
void
TvStateUpdate( char *UDN,
               int Service,
               IXML_Document * ChangedVariables,
               char **State )
{
    IXML_NodeList *properties,
     *variables;
    IXML_Element *property,
     *variable;
    int length,
      length1;
    int i,
      j;
    char *tmpstate = NULL;

    SampleUtil_Print( "Tv State Update (service %d): ", Service );

    /*
       Find all of the e:property tags in the document 
     */
    properties =
        ixmlDocument_getElementsByTagName( ChangedVariables,
                                           "e:property" );
    if( NULL != properties ) {
        length = ixmlNodeList_length( properties );
        for( i = 0; i < length; i++ ) { /* Loop through each property change found */
            property =
                ( IXML_Element * ) ixmlNodeList_item( properties, i );

            /*
               For each variable name in the state table, check if this
               is a corresponding property change 
             */
            for( j = 0; j < TvVarCount[Service]; j++ ) {
                variables =
                    ixmlElement_getElementsByTagName( property,
                                                      TvVarName[Service]
                                                      [j] );

                /*
                   If a match is found, extract the value, and update the state table 
                 */
                if( variables ) {
                    length1 = ixmlNodeList_length( variables );
                    if( length1 ) {
                        variable =
                            ( IXML_Element * )
                            ixmlNodeList_item( variables, 0 );
                        tmpstate = SampleUtil_GetElementValue( variable );

                        if( tmpstate ) {
                            strcpy( State[j], tmpstate );
                            SampleUtil_Print
                                ( " Variable Name: %s New Value:'%s'",
                                  TvVarName[Service][j], State[j] );
                        }

                        if( tmpstate )
                            free( tmpstate );
                        tmpstate = NULL;
                    }

                    ixmlNodeList_free( variables );
                    variables = NULL;
                }
            }

        }
        ixmlNodeList_free( properties );
    }
}

/********************************************************************************
 * TvCtrlPointHandleEvent
 *
 * Description: 
 *       Handle a UPnP event that was received.  Process the event and update
 *       the appropriate service state table.
 *
 * Parameters:
 *   sid -- The subscription id for the event
 *   eventkey -- The eventkey number for the event
 *   changes -- The DOM document representing the changes
 *
 ********************************************************************************/
void
TvCtrlPointHandleEvent( Upnp_SID sid,
                        int evntkey,
                        IXML_Document * changes )
{
    struct TvDeviceNode *tmpdevnode;
    int service;

    ithread_mutex_lock( &DeviceListMutex );

    tmpdevnode = GlobalDeviceList;
    while( tmpdevnode ) {
        for( service = 0; service < TV_SERVICE_SERVCOUNT; service++ ) {
            if( strcmp( tmpdevnode->device.TvService[service].SID, sid ) ==
                0 ) {
                SampleUtil_Print( "Received Tv %s Event: %d for SID %s",
                                  TvServiceName[service], evntkey, sid );
#if 0
                TvStateUpdate( tmpdevnode->device.UDN, service, changes,
                               ( char ** )&tmpdevnode->device.
                               TvService[service].VariableStrVal );
#endif
                break;
            }
        }
        tmpdevnode = tmpdevnode->next;
    }

    ithread_mutex_unlock( &DeviceListMutex );
}

/********************************************************************************
 * TvCtrlPointHandleSubscribeUpdate
 *
 * Description: 
 *       Handle a UPnP subscription update that was received.  Find the 
 *       service the update belongs to, and update its subscription
 *       timeout.
 *
 * Parameters:
 *   eventURL -- The event URL for the subscription
 *   sid -- The subscription id for the subscription
 *   timeout  -- The new timeout for the subscription
 *
 ********************************************************************************/
void
TvCtrlPointHandleSubscribeUpdate( char *eventURL,
                                  Upnp_SID sid,
                                  int timeout )
{
    struct TvDeviceNode *tmpdevnode;
    int service;

    ithread_mutex_lock( &DeviceListMutex );

    tmpdevnode = GlobalDeviceList;
    while( tmpdevnode ) {
        for( service = 0; service < TV_SERVICE_SERVCOUNT; service++ ) {

            if( strcmp
                ( tmpdevnode->device.TvService[service].EventURL,
                  eventURL ) == 0 ) {
                SampleUtil_Print
                    ( "Received Tv %s Event Renewal for eventURL %s",
                      TvServiceName[service], eventURL );
                strcpy( tmpdevnode->device.TvService[service].SID, sid );
                break;
            }
        }

        tmpdevnode = tmpdevnode->next;
    }

    ithread_mutex_unlock( &DeviceListMutex );
}

void
TvCtrlPointHandleGetVar( char *controlURL,
                         char *varName,
                         DOMString varValue )
{

    struct TvDeviceNode *tmpdevnode;
    int service;

    ithread_mutex_lock( &DeviceListMutex );

    tmpdevnode = GlobalDeviceList;
    while( tmpdevnode ) {
        for( service = 0; service < TV_SERVICE_SERVCOUNT; service++ ) {
            if( strcmp
                ( tmpdevnode->device.TvService[service].ControlURL,
                  controlURL ) == 0 ) {
                SampleUtil_StateUpdate( varName, varValue,
                                        tmpdevnode->device.UDN,
                                        GET_VAR_COMPLETE );
                break;
            }
        }
        tmpdevnode = tmpdevnode->next;
    }

    ithread_mutex_unlock( &DeviceListMutex );
}

/********************************************************************************
 * TvCtrlPointCallbackEventHandler
 *
 * Description: 
 *       The callback handler registered with the SDK while registering
 *       the control point.  Detects the type of callback, and passes the 
 *       request on to the appropriate function.
 *
 * Parameters:
 *   EventType -- The type of callback event
 *   Event -- Data structure containing event data
 *   Cookie -- Optional data specified during callback registration
 *
 ********************************************************************************/
int
TvCtrlPointCallbackEventHandler( Upnp_EventType EventType,
                                 void *Event,
                                 void *Cookie )
{
//    SampleUtil_PrintEvent( EventType, Event );

    switch ( EventType ) {
            /*
               SSDP Stuff 
             */
		case UPNP_DISCOVERY_ADVERTISEMENT_ALIVE:
		case UPNP_DISCOVERY_SEARCH_RESULT: {
			struct Upnp_Discovery *d_event = (struct Upnp_Discovery *)Event;
			IXML_Document *DescDoc = NULL;
			char *UDN = NULL;
			struct TvDevice *tvdevice = NULL;
			struct TvDeviceNode *deviceNode;
			int ret;

			if (d_event->ErrCode != UPNP_E_SUCCESS) {
				printf("Error in Discovery Callback -- %d\n", d_event->ErrCode);
				break;
			}

			ret = UpnpDownloadXmlDoc(d_event->Location, &DescDoc);
			if (ret != UPNP_E_SUCCESS) {
				printf("Error obtaining device description from %s -- error = %d\n",
						d_event->Location, ret);
				break;
			}

			UDN = SampleUtil_GetFirstDocumentItem(DescDoc, "UDN");
			ithread_mutex_lock(&DeviceListMutex);
			tvdevice = CtrlPointSearchDeviceListByUDN(UDN);
			if (tvdevice) {
				// The device is already there, so just update 
				// the advertisement timeout field
				tvdevice->AdvrTimeOut = d_event->Expires;
				ithread_mutex_unlock(&DeviceListMutex);
				ixmlDocument_free(DescDoc);
			} else {
				ithread_mutex_unlock(&DeviceListMutex);
				deviceNode = TvCtrlPointAddDevice(d_event, DescDoc, UDN);
				ithread_mutex_lock(&DeviceListMutex);
				if (deviceNode)
					CtrlPointAddDeviceList(deviceNode);
				ithread_mutex_unlock(&DeviceListMutex);
			}
			if (UDN)
				free(UDN);

			TvCtrlPointPrintList();
			break;
		}
        case UPNP_DISCOVERY_SEARCH_TIMEOUT:
            /*
               Nothing to do here... 
             */
            break;

        case UPNP_DISCOVERY_ADVERTISEMENT_BYEBYE:
            {
                struct Upnp_Discovery *d_event =
                    ( struct Upnp_Discovery * )Event;

                if( d_event->ErrCode != UPNP_E_SUCCESS ) {
                    SampleUtil_Print
                        ( "Error in Discovery ByeBye Callback -- %d",
                          d_event->ErrCode );
                }

                SampleUtil_Print( "Received ByeBye for Device: %s",
                                  d_event->DeviceId );
                TvCtrlPointRemoveDevice( d_event->DeviceId );

                SampleUtil_Print( "After byebye:" );
                TvCtrlPointPrintList();

                break;
            }

            /*
               SOAP Stuff 
             */
        case UPNP_CONTROL_ACTION_COMPLETE:
            {
                struct Upnp_Action_Complete *a_event =
                    ( struct Upnp_Action_Complete * )Event;

                if( a_event->ErrCode != UPNP_E_SUCCESS ) {
                    SampleUtil_Print
                        ( "Error in  Action Complete Callback -- %d",
                          a_event->ErrCode );
                }

                /*
                   No need for any processing here, just print out results.  Service state
                   table updates are handled by events. 
                 */

                break;
            }

        case UPNP_CONTROL_GET_VAR_COMPLETE:
            {
                struct Upnp_State_Var_Complete *sv_event =
                    ( struct Upnp_State_Var_Complete * )Event;

                if( sv_event->ErrCode != UPNP_E_SUCCESS ) {
                    SampleUtil_Print
                        ( "Error in Get Var Complete Callback -- %d",
                          sv_event->ErrCode );
                } else {
                    TvCtrlPointHandleGetVar( sv_event->CtrlUrl,
                                             sv_event->StateVarName,
                                             sv_event->CurrentVal );
                }

                break;
            }

            /*
               GENA Stuff 
             */
        case UPNP_EVENT_RECEIVED:
            {
                struct Upnp_Event *e_event = ( struct Upnp_Event * )Event;

                TvCtrlPointHandleEvent( e_event->Sid, e_event->EventKey,
                                        e_event->ChangedVariables );
                break;
            }

        case UPNP_EVENT_SUBSCRIBE_COMPLETE:
        case UPNP_EVENT_UNSUBSCRIBE_COMPLETE:
        case UPNP_EVENT_RENEWAL_COMPLETE:
            {
                struct Upnp_Event_Subscribe *es_event =
                    ( struct Upnp_Event_Subscribe * )Event;

                if( es_event->ErrCode != UPNP_E_SUCCESS ) {
                    SampleUtil_Print
                        ( "Error in Event Subscribe Callback -- %d",
                          es_event->ErrCode );
                } else {
                    TvCtrlPointHandleSubscribeUpdate( es_event->
                                                      PublisherUrl,
                                                      es_event->Sid,
                                                      es_event->TimeOut );
                }

                break;
            }

        case UPNP_EVENT_AUTORENEWAL_FAILED:
        case UPNP_EVENT_SUBSCRIPTION_EXPIRED:
            {
                int TimeOut = default_timeout;
                Upnp_SID newSID;
                int ret;

                struct Upnp_Event_Subscribe *es_event =
                    ( struct Upnp_Event_Subscribe * )Event;

                ret =
                    UpnpSubscribe( ctrlpt_handle, es_event->PublisherUrl,
                                   &TimeOut, newSID );

                if( ret == UPNP_E_SUCCESS ) {
                    SampleUtil_Print( "Subscribed to EventURL with SID=%s",
                                      newSID );
                    TvCtrlPointHandleSubscribeUpdate( es_event->
                                                      PublisherUrl, newSID,
                                                      TimeOut );
                } else {
                    SampleUtil_Print
                        ( "Error Subscribing to EventURL -- %d", ret );
                }
                break;
            }

            /*
               ignore these cases, since this is not a device 
             */
        case UPNP_EVENT_SUBSCRIPTION_REQUEST:
        case UPNP_CONTROL_GET_VAR_REQUEST:
        case UPNP_CONTROL_ACTION_REQUEST:
            break;
    }

    return 0;
}

/********************************************************************************
 * TvCtrlPointVerifyTimeouts
 *
 * Description: 
 *       Checks the advertisement  each device
 *        in the global device list.  If an advertisement expires,
 *       the device is removed from the list.  If an advertisement is about to
 *       expire, a search request is sent for that device.  
 *
 * Parameters:
 *    incr -- The increment to subtract from the timeouts each time the
 *            function is called.
 *
 ********************************************************************************/
void
TvCtrlPointVerifyTimeouts( int incr )
{
    struct TvDeviceNode *prevdevnode,
     *curdevnode;
    int ret;

    ithread_mutex_lock( &DeviceListMutex );

    prevdevnode = NULL;
    curdevnode = GlobalDeviceList;

    while( curdevnode ) {
        curdevnode->device.AdvrTimeOut -= incr;
        //SampleUtil_Print("Advertisement Timeout: %d\n", curdevnode->device.AdvrTimeOut);

        if( curdevnode->device.AdvrTimeOut <= 0 ) {
            /*
               This advertisement has expired, so we should remove the device
               from the list 
             */

            if( GlobalDeviceList == curdevnode )
                GlobalDeviceList = curdevnode->next;
            else
                prevdevnode->next = curdevnode->next;
            TvCtrlPointDeleteNode( curdevnode );
            if( prevdevnode )
                curdevnode = prevdevnode->next;
            else
                curdevnode = GlobalDeviceList;
        } else {

            if( curdevnode->device.AdvrTimeOut < 2 * incr ) {
                /*
                   This advertisement is about to expire, so send
                   out a search request for this device UDN to 
                   try to renew 
                 */
                ret = UpnpSearchAsync( ctrlpt_handle, incr,
                                       curdevnode->device.UDN, NULL );
                if( ret != UPNP_E_SUCCESS )
                    SampleUtil_Print
                        ( "Error sending search request for Device UDN: %s -- err = %d",
                          curdevnode->device.UDN, ret );
            }

            prevdevnode = curdevnode;
            curdevnode = curdevnode->next;
        }

    }
    ithread_mutex_unlock( &DeviceListMutex );

}

/********************************************************************************
 * TvCtrlPointTimerLoop
 *
 * Description: 
 *       Function that runs in its own thread and monitors advertisement
 *       and subscription timeouts for devices in the global device list.
 *
 * Parameters:
 *    None
 *
 ********************************************************************************/
void *
TvCtrlPointTimerLoop( void *args )
{
    int incr = 30;              // how often to verify the timeouts, in seconds

    while( 1 ) {
        isleep( incr );
        TvCtrlPointVerifyTimeouts( incr );
    }

    return NULL;
}

/********************************************************************************
 * TvCtrlPointStart
 *
 * Description: 
 *		Call this function to initialize the UPnP library and start the TV Control
 *		Point.  This function creates a timer thread and provides a callback
 *		handler to process any UPnP events that are received.
 *
 * Parameters:
 *		None
 *
 * Returns:
 *		TV_SUCCESS if everything went well, else TV_ERROR
 *
 ********************************************************************************/
int
TvCtrlPointStart( print_string printFunctionPtr,
                  state_update updateFunctionPtr )
{
    ithread_t timer_thread;
    int rc;
    unsigned short port = 0;
    char *ip_address = NULL;

    SampleUtil_Initialize( printFunctionPtr );
    SampleUtil_RegisterUpdateFunction( updateFunctionPtr );

    ithread_mutex_init( &DeviceListMutex, 0 );

    SampleUtil_Print(
        "Initializing UPnP Sdk with\n"
        "\tipaddress = %s port = %u\n",
        ip_address, port );

    rc = UpnpInit( ip_address, port );
    if( UPNP_E_SUCCESS != rc ) {
        SampleUtil_Print( "WinCEStart: UpnpInit() Error: %d", rc );
        UpnpFinish();
        return TV_ERROR;
    }

    if( NULL == ip_address ) {
        ip_address = UpnpGetServerIpAddress();
    }
    if( 0 == port ) {
        port = UpnpGetServerPort();
    }

    SampleUtil_Print(
        "UPnP Initialized\n"
	"\tipaddress= %s port = %u\n",
        ip_address, port );

    SampleUtil_Print( "Registering Control Point" );
    rc = UpnpRegisterClient( TvCtrlPointCallbackEventHandler,
                             &ctrlpt_handle, &ctrlpt_handle );
    if( UPNP_E_SUCCESS != rc ) {
        SampleUtil_Print( "Error registering CP: %d", rc );
        UpnpFinish();
        return TV_ERROR;
    }

    SampleUtil_Print( "Control Point Registered" );

    TvCtrlPointRefresh();

    // start a timer thread
    ithread_create( &timer_thread, NULL, TvCtrlPointTimerLoop, NULL );

    return TV_SUCCESS;
}

int
TvCtrlPointStop( void )
{
    TvCtrlPointRemoveAll();
    UpnpUnRegisterClient( ctrlpt_handle );
    UpnpFinish();
    SampleUtil_Finish();

    return TV_SUCCESS;
}
