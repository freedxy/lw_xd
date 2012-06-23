
IXML_Document *get_browse_result(int devnum, int service, int ObjectID)
{
	char *param_name[] = {
		"ObjectID",
		"BrowseFlag",
		"Filter",
		"StartingIndex",
		"RequestedCount",
		"SortCriteria",
	};
	char *param_val[] = {
		"",
		"BrowseDirectChildren",
		"*",
		"0",
		"0",
		"",
	};
	struct TvDeviceNode *devnode = NULL;
	char param_val_tmp[64];

	sprintf(param_val_tmp, "%d", ObjectID);
	param_val[0] = param_val_tmp;

	TvCtrlPointGetDevice(devnum, &devnode);
	if (!devnode)
		return NULL

	return CtrlPointSendAction(devnode->device.UDN, service, "Browse",
								&param_name[0], &param_val[0], 6);
}

