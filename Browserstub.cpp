/*
 * Copyright (C) 2014, Jaguar Land Rover
 *
 * Author: Jonatan Palsson <jonatan.palsson@pelagicore.com>
 *
 * This file is part of the GENIVI Media Manager Proof-of-Concept
 * For further information, see http://genivi.org/
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <v1/org/genivi/mediamanager/BrowserTypes.hpp>
#include "browserstub.h"
#include "common.h"
#include "player_logger.h"

namespace MM = ::v1::org::genivi::mediamanager;

namespace lge {
namespace mm {

/**
 * ================================================================================
 * @fn : BrowserStubImpl
 * @brief : constructor of BrowserStubImpl
 * @section : Function flow (Pseudo-code or Decision Table)
 *    pass pointer of BrowserStubImpl instance to BrowserProvider.
 * @param[in] provider : pointer of BrowserProvider instance.
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : none
 * ===================================================================================
 */
BrowserStubImpl::BrowserStubImpl (BrowserProvider *provider)
    :mBrowser(provider)
{
    mBrowser->setBrowserStub(this);
}

std::string sortKeyToString (MM::BrowserTypes::SortKey sk) {
    std::string keyStr;
    if (sk.getOrder() == MM::BrowserTypes::SortOrder::ASCENDING)
        keyStr += "+";
    else
        keyStr += "-";

    keyStr += sk.getKeyName();
    return keyStr;
}

/**
 * ================================================================================
 * @fn : discoverMediaManagers
 * @brief : do nothing
 * @section : Function flow (Pseudo-code or Decision Table)
 *    N/A
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : none
 * ===================================================================================
 */
void BrowserStubImpl::discoverMediaManagers(const std::shared_ptr<CommonAPI::ClientId> _client, discoverMediaManagersReply_t _reply)
{
    MMLogInfo("");
    std::vector<std::string> _identifiers;
    MM::BrowserTypes::BrowserError _e;

    _reply(_identifiers, _e);
}

/**
 * ================================================================================
 * @fn : listContainers
 * @brief : reqeust list of container objects.
 * @section : Function flow (Pseudo-code or Decision Table)
 *   call listContainers() of BrowserProvider.
 * @param[in] _path : object path
 * @param[in] _offset : offset of list window.
 * @param[in] _count : size of list window.
 * @param[in] _filter : column list to retrive.
 * @param[out] _containers : ResultMapList including list of containers.
 * @param[out] _e : result code (NO_ERROR/NO_CONNECTION/BAD_PATH)
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : none
 * ===================================================================================
 */
void BrowserStubImpl::listContainers(const std::shared_ptr<CommonAPI::ClientId> _client,
                                     std::string _path,
                                     uint64_t _offset,
                                     uint64_t _count,
                                     std::vector<std::string> _filter,
                                     listContainersReply_t _reply)
{
    MMLogInfo("");

    MM::BrowserTypes::BrowserError _e;
    MM::MediaTypes::ResultMapList _containers;

    if(_path.length() <= 0 ) {
        _e = MM::BrowserTypes::BrowserError::BAD_PATH;
        _reply(_containers, _e);
        return;
    }

    bool result = false;
    if(mBrowser)
        result = mBrowser->listContainers(_path, _offset, _count, _filter, _containers);

    if (result)
        _e = MM::BrowserTypes::BrowserError::NO_ERROR;
    else
        _e = MM::BrowserTypes::BrowserError::BAD_PATH;

    _reply(_containers, _e);
}

/**
 * ================================================================================
 * @fn : listContainersEx2
 * @brief : reqeust list of container objects.
 * @section : Function flow (Pseudo-code or Decision Table)
 *   call listContainers() of BrowserProvider.
 * @param[in] _path : object path
 * @param[in] _offset : offset of list window.
 * @param[in] _count : size of list window.
 * @param[in] _filter : column list to retrive.
 * @param[in] _sortKeys : key list for sorting.
 * @param[out] _containers : ResultMapList including list of containers.
 * @param[out] _e : result code (NO_ERROR/NO_CONNECTION/BAD_PATH)
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : none
 * ===================================================================================
 */
void BrowserStubImpl::listContainersEx2(const std::shared_ptr<CommonAPI::ClientId> _client,
                                       std::string _path,
                                       uint64_t _offset,
                                       uint64_t _count,
                                       std::vector<std::string> _filter,
                                       std::vector<MM::BrowserTypes::SortKey> _sortKeys,
                                       listContainersEx2Reply_t _reply)
{
    MM::BrowserTypes::BrowserError _e;
    MM::MediaTypes::ResultMapList _containers;

    if(_path.length() <= 0 ) {
        _e = MM::BrowserTypes::BrowserError::BAD_PATH;
        _reply(_containers, _e);
        return;
    }

    bool result = false;

    if(mBrowser)
        result = mBrowser->listContainersEx2(_path, _offset, _count, _filter, _sortKeys, _containers);


    if (result)
        _e = MM::BrowserTypes::BrowserError::NO_ERROR;
    else
        _e = MM::BrowserTypes::BrowserError::BAD_PATH;

    _reply(_containers, _e);

}

/**
 * ================================================================================
 * @fn : listContainersEx
 * @brief : reqeust list of container objects.
 * @section : Function flow (Pseudo-code or Decision Table)
 *   call listContainers() of BrowserProvider.
 * @param[in] _path : object path
 * @param[in] _offset : offset of list window.
 * @param[in] _count : size of list window.
 * @param[in] _filter : column list to retrive.
 * @param[in] _sortKey : key for sorting.
 * @param[out] _containers : ResultMapList including list of containers.
 * @param[out] _e : result code (NO_ERROR/NO_CONNECTION/BAD_PATH)
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : none
 * ===================================================================================
 */
void BrowserStubImpl::listContainersEx(const std::shared_ptr<CommonAPI::ClientId> _client,
                                       std::string _path,
                                       uint64_t _offset,
                                       uint64_t _count,
                                       std::vector<std::string> _filter,
                                       ::v1::org::genivi::mediamanager::BrowserTypes::SortKey _sortKey,
                                       listContainersExReply_t _reply)
{
    MM::BrowserTypes::BrowserError _e;
    MM::MediaTypes::ResultMapList _containers;

    if(_path.length() <= 0 ) {
        _e = MM::BrowserTypes::BrowserError::BAD_PATH;
        _reply(_containers, _e);
        return;
    }

    bool result = false;

    if(mBrowser)
        result = mBrowser->listContainersEx(_path, _offset, _count, _filter, _sortKey, _containers);


    if (result)
        _e = MM::BrowserTypes::BrowserError::NO_ERROR;
    else
        _e = MM::BrowserTypes::BrowserError::BAD_PATH;

    _reply(_containers, _e);

}

/**
 * ================================================================================
 * @fn : listChildrenEx
 * @brief : reqeust child objects of parent object.
 * @section : Function flow (Pseudo-code or Decision Table)
 *   call listChildren() of BrowserProvider.
 * @param[in] _path : object path
 * @param[in] _offset : offset of list window.
 * @param[in] _count : size of list window.
 * @param[in] _filter : column list to retrive.
 * @param[in] _sortKey : key for sorting.
 * @param[out] _children : ResultMapList including list of children.
 * @param[out] _e : result code (NO_ERROR/NO_CONNECTION/BAD_PATH)
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : none
 * ===================================================================================
 */
void BrowserStubImpl::listChildrenEx(const std::shared_ptr<CommonAPI::ClientId> _client,
                                     std::string _path,
                                     uint64_t _offset,
                                     uint64_t _count,
                                     std::vector<std::string> _filter,
                                     ::v1::org::genivi::mediamanager::BrowserTypes::SortKey _sortKey,
                                     listChildrenExReply_t _reply)
{
    MMLogInfo("path=[%s]", _path.c_str());
    MM::BrowserTypes::BrowserError _e;
    MM::MediaTypes::ResultMapList _children;
    //TO-DO
    _reply(_children, _e);

}

/**
 * ================================================================================
 * @fn : listChildrenEx
 * @brief : reqeust child objects of parent object.
 * @section : Function flow (Pseudo-code or Decision Table)
 *   call listChildrenEx2() of BrowserProvider.
 * @param[in] _path : object path
 * @param[in] _offset : offset of list window.
 * @param[in] _count : size of list window.
 * @param[in] _filter : column list to retrive.
 * @param[in] _sortKeys : key list for sorting.
 * @param[out] A_children : ResultMapList including list of children.
 * @param[out] _e : result code (NO_ERROR/NO_CONNECTION/BAD_PATH)
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : none
 * ===================================================================================
 */
void BrowserStubImpl::listChildrenEx2(const std::shared_ptr<CommonAPI::ClientId> _client,
                     std::string _path, uint64_t _offset, uint64_t _count,
                     std::vector<std::string> _filter,
                     std::vector<MM::BrowserTypes::SortKey> _sortKeys,
                     listChildrenEx2Reply_t _reply)
{
    MMLogInfo("path=[%s]", _path.c_str());
    MM::BrowserTypes::BrowserError _e;
    MM::MediaTypes::ResultMapList _children;

    //filesystem
    if(_path.length() <= 0 ) {
        _e = MM::BrowserTypes::BrowserError::BAD_PATH;
        _reply(_children, _e);
        return;
    }

    bool result = false;

    if(mBrowser)
        result = mBrowser->listChildrenEx2(_path, _offset, _count, _filter, _sortKeys, _children);

    if (result)
        _e = MM::BrowserTypes::BrowserError::NO_ERROR;
    else
        _e = MM::BrowserTypes::BrowserError::BAD_PATH;

    _reply(_children, _e);
}

/**
 * ================================================================================
 * @fn : listChildren
 * @brief : reqeust child objects of parent object.
 * @section : Function flow (Pseudo-code or Decision Table)
 *   call listChildren() of BrowserProvider.
 * @param[in] _path : object path
 * @param[in] _offset : offset of list window.
 * @param[in] _count : size of list window.
 * @param[in] _filter : column list to retrive.
 * @param[out] _children : ResultMapList including list of children.
 * @param[out] _e : result code (NO_ERROR/NO_CONNECTION/BAD_PATH)
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : none
 * ===================================================================================
 */
void BrowserStubImpl::listChildren(const std::shared_ptr<CommonAPI::ClientId> _client,
                                   std::string _path,
                                   uint64_t _offset,
                                   uint64_t _count,
                                   std::vector<std::string> _filter,
                                   listChildrenReply_t _reply)
{
    MMLogInfo("path=[%s]", _path.c_str());
    MM::BrowserTypes::BrowserError _e = MM::BrowserTypes::BrowserError::NO_ERROR;
    MM::MediaTypes::ResultMapList _children;

    if(_path.length() <= 0 ) {
        _e = MM::BrowserTypes::BrowserError::BAD_PATH;
        _reply(_children, _e);
        return;
    }

    bool result = false;
    if(mBrowser)
        result = mBrowser->listChildren(_path, _offset, _count, _filter, _children);

    _reply(_children, _e);
}

/**
 * ================================================================================
 * @fn : listItems
 * @brief : reqeust children list of parent object.
 *
 * @section : Function flow (Pseudo-code or Decision Table)
 *   call listItems() of BrowserProvider.
 * @param[in] _path : object path
 * @param[in] _offset : offset of list window.
 * @param[in] _count : size of list window.
 * @param[in] _filter : column list to retrive.
 * @param[out] _items : ResultMapList including list of items.
 * @param[out] _e : result code (NO_ERROR/NO_CONNECTION/BAD_PATH)
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : none
 * ===================================================================================
 */
void BrowserStubImpl::listItems(const std::shared_ptr<CommonAPI::ClientId> _client,
                                std::string _path,
                                uint64_t _offset,
                                uint64_t _count,
                                std::vector<std::string> _filter,
                                listItemsReply_t _reply) {

    MMLogInfo("path=[%s]", _path.c_str());
    MM::BrowserTypes::BrowserError _e;
    MM::MediaTypes::ResultMapList _items;

    //filesystem
    if(_path.length() <= 0 ) {
        _e = MM::BrowserTypes::BrowserError::BAD_PATH;
        _reply(_items, _e);
        return;
    }

    bool result = false;

    if(mBrowser)
        result = mBrowser->listItems(_path, _offset, _count, _filter, _items);

    if (result)
        _e = MM::BrowserTypes::BrowserError::NO_ERROR;
    else
        _e = MM::BrowserTypes::BrowserError::BAD_PATH;

    _reply(_items, _e);
}

/**
 * ================================================================================
 * @fn : listItemsEx
 * @brief : reqeust item object list.
 * @section : Function flow (Pseudo-code or Decision Table)
 *   call listItemsEx() of BrowserProvider.
 * @param[in] _path : object path
 * @param[in] _offset : offset of list window.
 * @param[in] _count : size of list window.
 * @param[in] _filter : column list to retrive.
 * @param[in] _sortKey : key for sorting.
 * @param[out] _items : ResultMapList including list of items.
 * @param[out] _e : result code (NO_ERROR/NO_CONNECTION/BAD_PATH)
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : none
 * ===================================================================================
 */
void BrowserStubImpl::listItemsEx(const std::shared_ptr<CommonAPI::ClientId> _client,
                                  std::string _path,
                                  uint64_t _offset,
                                  uint64_t _count,
                                  std::vector<std::string> _filter,
                                  ::v1::org::genivi::mediamanager::BrowserTypes::SortKey _sortKey,
                                  listContainersReply_t _reply)
{
    MMLogInfo("path=[%s]", _path.c_str());
    MM::BrowserTypes::BrowserError _e;
    MM::MediaTypes::ResultMapList _items;

    //filesystem
    if(_path.length() <= 0 ) {
        _e = MM::BrowserTypes::BrowserError::BAD_PATH;
        _reply(_items, _e);
        return;
    }

    bool result = false;

    if(mBrowser)
        result = mBrowser->listItemsEx(_path, _offset, _count, _filter, _sortKey, _items);

    if (result)
        _e = MM::BrowserTypes::BrowserError::NO_ERROR;
    else
        _e = MM::BrowserTypes::BrowserError::BAD_PATH;

    _reply(_items, _e);

}

/**
 * ================================================================================
 * @fn : listItemsEx2
 * @brief : reqeust item object list.
 * @section : Function flow (Pseudo-code or Decision Table)
 *   call listItemsEx2() of BrowserProvider.
 * @param[in] _path : object path
 * @param[in] _offset : offset of list window.
 * @param[in] _count : size of list window.
 * @param[in] _filter : column list to retrive.
 * @param[in] _sortKeys : key list for sorting.
 * @param[out] _items : ResultMapList including list of items.
 * @param[out] _e : result code (NO_ERROR/NO_CONNECTION/BAD_PATH)
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : none
 * ===================================================================================
 */
void BrowserStubImpl::listItemsEx2(const std::shared_ptr<CommonAPI::ClientId> _client,
                  std::string _path, uint64_t _offset, uint64_t _count,
                  std::vector<std::string> _filter,
                  std::vector<MM::BrowserTypes::SortKey> _sortKeys,
                  listItemsEx2Reply_t _reply)
{
    MMLogInfo("path=[%s]", _path.c_str());
    MM::BrowserTypes::BrowserError _e;
    MM::MediaTypes::ResultMapList _items;
    //filesystem
    if(_path.length() <= 0 ) {
        _e = MM::BrowserTypes::BrowserError::BAD_PATH;
        _reply(_items, _e);
        return;
    }

    bool result = false;

    if(mBrowser)
        result = mBrowser->listItemsEx2(_path, _offset, _count, _filter, _sortKeys, _items);

    if (result)
        _e = MM::BrowserTypes::BrowserError::NO_ERROR;
    else
        _e = MM::BrowserTypes::BrowserError::BAD_PATH;

    _reply(_items, _e);
}

void BrowserStubImpl::createReference(const std::shared_ptr<CommonAPI::ClientId> _client,
                                      std::string _path,
                                      std::string _objectPath,
                                      createReferenceReply_t _reply)
{
  MMLogInfo("");
}

void BrowserStubImpl::createContainer(const std::shared_ptr<CommonAPI::ClientId> _client,
                                      std::string _path,
                                      std::string _displayName,
                                      std::vector<std::string> _childTypes,
                                      createContainerReply_t _reply)
{
  MMLogInfo("");
}

/**
 * ================================================================================
 * @fn : searchObjects
 * @brief : search item and container objects.
 * @section : Function flow (Pseudo-code or Decision Table)
 *   call searchObjects() of BrowserProvider.
 * @param[in] _path : object path
 * @param[in] _query : search condition.
 * @param[in] _offset : offset of list window.
 * @param[in] _count : size of list window.
 * @param[in] _filter : column list to retrive.
 * @param[out] _items : ResultMapList including list of items/containers.
 * @param[out] _e : result code (NO_ERROR/NO_CONNECTION/BAD_PATH)
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : none
 * ===================================================================================
 */
void BrowserStubImpl::searchObjects(const std::shared_ptr<CommonAPI::ClientId> _client,
                                    std::string _path,
                                    std::string _query,
                                    uint64_t _offset,
                                    uint64_t _count,
                                    std::vector<std::string> _filter,
                                    searchObjectsReply_t _reply)
{
    MMLogInfo("");
    MM::BrowserTypes::BrowserError _e;
    MM::MediaTypes::ResultMapList _items;

    if(_path.length() <= 0 ) {
        _e = MM::BrowserTypes::BrowserError::BAD_PATH;
        return;
    }

    bool result = false;

    if(mBrowser)
        result = mBrowser->searchObjects(_path, _query, _offset, _count, _filter, _items);

    if (result)
        _e = MM::BrowserTypes::BrowserError::NO_ERROR;
    else
        _e = MM::BrowserTypes::BrowserError::BAD_PATH;

    _reply(_items, _e);
}

/**
 * ================================================================================
 * @fn : searchObjectsEx
 * @brief : search item and container objects, make sort list with sort key.
 * @section : Function flow (Pseudo-code or Decision Table)
 *   call searchObjectsEx() of BrowserProvider.
 * @param[in] _path : object path
 * @param[in] _query : search condition.
 * @param[in] _offset : offset of list window.
 * @param[in] _count : size of list window.
 * @param[in] _filter : column list to retrive.
 * @param[in] _sortKey : key for sorting.
 * @param[out] _items : ResultMapList including list of items/containers.
 * @param[out] _e : result code (NO_ERROR/NO_CONNECTION/BAD_PATH)
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : none
 * ===================================================================================
 */
void BrowserStubImpl::searchObjectsEx(const std::shared_ptr<CommonAPI::ClientId> _client,
                                      std::string _path,
                                      std::string _query,
                                      uint64_t _offset,
                                      uint64_t _count,
                                      std::vector<std::string> _filter,
                                      ::v1::org::genivi::mediamanager::BrowserTypes::SortKey _sortKey,
                                      searchObjectsExReply_t _reply)
{
    MMLogInfo("");
    MM::BrowserTypes::BrowserError _e;
    MM::MediaTypes::ResultMapList _items;

    if(_path.length() <= 0 ) {
        _e = MM::BrowserTypes::BrowserError::BAD_PATH;
        return;
    }

    bool result = false;
    if(mBrowser)
        result = mBrowser->searchObjectsEx(_path, _query, _offset, _count, _filter, _sortKey, _items);

    if (result)
        _e = MM::BrowserTypes::BrowserError::NO_ERROR;
    else
        _e = MM::BrowserTypes::BrowserError::BAD_PATH;

    _reply(_items, _e);

}

/**
 * ================================================================================
 * @fn : getUsbDeviceList
 * @brief :  get External USB/MTP device list
 * @section : Function flow (Pseudo-code or Decision Table)
 *      call getMountedList of BrowserProvider.
 * @param[out] _deviceInfoList : information about connected USB/MTP device.
 *               device type / mount path / port / device name / product name
 * @param[out] _e : result code (NO_ERROR/NO_CONNECTION/BAD_PATH)
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : none
 * ===================================================================================
 */
void BrowserStubImpl::getUsbDeviceList(const std::shared_ptr<CommonAPI::ClientId> _client,
                                       getUsbDeviceListReply_t _reply){
  MMLogInfo("");
  std::vector<MM::Browser::UsbDeviceInfo> _deviceInfoList;
  MM::BrowserTypes::BrowserError _e;

    bool result = false;
  if(mBrowser)
    result = mBrowser->getMountedList(_deviceInfoList);

  if (result)
    _e = MM::BrowserTypes::BrowserError::NO_ERROR;
  else
    _e = MM::BrowserTypes::BrowserError::BAD_PATH;

  _reply(_deviceInfoList, _e);
}

/**
 * ================================================================================
 * @fn : getUsbMediaFileInfo
 * @brief :  get media file count information of USB/MTP device.
 * @section : Function flow (Pseudo-code or Decision Table)
 *      call getMediaFileInfo of BrowserProvider.
 * @param[out] _mediaInfoList : media file information about connected USB/MTP device.
 *               device type / mount path / audio count / video count / photo count.
 * @param[out] _e : result code (NO_ERROR/NO_CONNECTION/BAD_PATH)
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : none
 * ===================================================================================
 */
void BrowserStubImpl::getUsbMediaFileInfo(const std::shared_ptr<CommonAPI::ClientId> _client,
                                          getUsbMediaFileInfoReply_t _reply)
{
    MMLogInfo("");
    std::vector<MM::Browser::UsbMediaFileInfo> _mediaInfoList;
    MM::BrowserTypes::BrowserError _e;

    bool result = false;
    if(mBrowser)
        result = mBrowser->getMediaFileInfo(_mediaInfoList);

    if (result)
        _e = MM::BrowserTypes::BrowserError::NO_ERROR;
    else
        _e = MM::BrowserTypes::BrowserError::BAD_PATH;

    _reply(_mediaInfoList, _e);
}

/**
 * ================================================================================
 * @fn : scan
 * @brief :  scan files/directories of input device path.
 * @section : Function flow (Pseudo-code or Decision Table)
 *  call scan of BrowserProvider.
 * @param[in] _path : device path to scan.
 * @param[out] _e : result code (NO_ERROR/NO_CONNECTION/BAD_PATH)
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : none
 * ===================================================================================
 */
void BrowserStubImpl::scanFilesystem(const std::shared_ptr<CommonAPI::ClientId> _client,
                        std::string _path,
                        scanFilesystemReply_t _reply)
{
    MMLogInfo("");
    MM::BrowserTypes::BrowserError _e;
    bool result = false;
    if(mBrowser)
        result = mBrowser->scan(_path);

    if (result)
        _e = MM::BrowserTypes::BrowserError::NO_ERROR;
    else
        _e = MM::BrowserTypes::BrowserError::BAD_PATH;

    _reply();
}

void BrowserStubImpl::usbMounted(const std::shared_ptr<CommonAPI::ClientId> _client,
    std::string _path,
    uint32_t _port,
    std::string _label,
    std::string _productName,
    usbMountedReply_t _reply) {

    MMLogInfo("");
    MM::BrowserTypes::BrowserError _e;
    bool result = false;
    if(mBrowser)
        result = mBrowser->usbMounted(_path, _port, _label, _productName);

    if (result)
        _e = MM::BrowserTypes::BrowserError::NO_ERROR;
    else
        _e = MM::BrowserTypes::BrowserError::BAD_PATH;
    _reply();
}

void BrowserStubImpl::usbUnMounted(const std::shared_ptr<CommonAPI::ClientId> _client,
    std::string _path,
    uint32_t _port,
    std::string _label,
    std::string _productName,
    usbUnMountedReply_t _reply) {

    MMLogInfo("");
    MM::BrowserTypes::BrowserError _e;
    bool result = false;
    if(mBrowser)
        result = mBrowser->usbUnMounted(_path, _port, _label, _productName);

    if (result)
        _e = MM::BrowserTypes::BrowserError::NO_ERROR;
    else
        _e = MM::BrowserTypes::BrowserError::BAD_PATH;
    _reply();
}

/**
 * ================================================================================
 * @fn : requestThumbnails
 * @brief :  request video thumbnail extraction to ExtractorInterface.
 * @section : Function flow (Pseudo-code or Decision Table)
 *  call requestImageExtraction of BrowserProvider.
 * @param[in] _paths : list of video file path.
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : none
 * ===================================================================================
 */
void BrowserStubImpl::requestThumbnails(const std::shared_ptr<CommonAPI::ClientId> _client, std::vector<std::string> _paths,
                                        v1::org::genivi::mediamanager::BrowserStub::requestThumbnailsReply_t _reply)
{
    MMLogInfo("");
    if(mBrowser)
        mBrowser->requestImageExtraction(_paths);

    _reply();
}

void BrowserStubImpl::setPlayTime(const std::shared_ptr<CommonAPI::ClientId> _client, std::string _path, uint32_t _playTime,
                                  v1::org::genivi::mediamanager::BrowserStub::setPlayTimeReply_t _reply)
{
    MMLogInfo("");
    MM::BrowserTypes::BrowserError _e;

    bool result = false;
    if(mBrowser)
        result = mBrowser->setPlayTime(_path, _playTime);

    if (result)
        _e = MM::BrowserTypes::BrowserError::NO_ERROR;
    else
        _e = MM::BrowserTypes::BrowserError::BAD_PATH;

    _reply(_e);
}

/**
 * ================================================================================
 * @fn : requestCoverArts
 * @brief :  request audio metadata information to ExtractorInterface.
 * @section : Function flow (Pseudo-code or Decision Table)
 *  convert  std::vector<MM::Browser::CoverArtKey> type to QList<StringList>
 *  call requestCoverArts of BrowserProvider.
 * @param[in] _keys : list of input key values(couple of artist, album)
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : none
 * ===================================================================================
 */
void BrowserStubImpl::requestCoverArts(const std::shared_ptr<CommonAPI::ClientId> _client,
                                       std::vector<MM::Browser::CoverArtKey> _keys,
                                       requestCoverArtsReply_t _reply)
{

    std::vector<std::vector<std::string>> coverArtKeys;

    std::vector<MM::Browser::CoverArtKey>::iterator iter;
    for(iter = _keys.begin(); iter != _keys.end(); iter++)
    {
        MM::Browser::CoverArtKey coverArtKey = (*iter);
        std::vector<std::string> newKey;
        std::string artist = coverArtKey.getArtist();
        std::string album = coverArtKey.getAlbum();
        newKey.push_back(album); newKey.push_back(artist);

        coverArtKeys.push_back(newKey);
    }

    if(mBrowser)
        mBrowser->requestCoverArts(coverArtKeys);

  _reply();
}

/**
 * ================================================================================
 * @fn : stopCoverArts
 * @brief :  request thumbnail-extractor to stop extracting audio cover art.
 * @section : Function flow (Pseudo-code or Decision Table)
 *  call stopCoverArts() of BrowserProvider.
 * @param[out] _e : result code (NO_ERROR/NO_CONNECTION/BAD_PATH)
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : none
 * ===================================================================================
 */
void BrowserStubImpl::stopCoverArts(const std::shared_ptr<CommonAPI::ClientId> _client,
                                    stopCoverArtsReply_t _reply)
{
    MM::BrowserTypes::BrowserError _e;
    bool result = false;
    result = mBrowser->stopCoverArts();

    if (result)
        _e = MM::BrowserTypes::BrowserError::NO_ERROR;
    else
        _e = MM::BrowserTypes::BrowserError::BAD_PATH;

    _reply(_e);
}

/**
 * ================================================================================
 * @fn : stopThumbnails
 * @brief :  request thumbnail-extractor to stop extracting video thumbnail.
 * @section : Function flow (Pseudo-code or Decision Table)
 *  call stopThumbnails() of BrowserProvider.
 * @param[out] _e : result code (NO_ERROR/NO_CONNECTION/BAD_PATH)
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : none
 * ===================================================================================
 */
void BrowserStubImpl::stopThumbnails(const std::shared_ptr<CommonAPI::ClientId> _client,
                                     stopThumbnailsReply_t _reply)
{
    MM::BrowserTypes::BrowserError _e;
    bool result = false;

    result = mBrowser->stopThumbnails();

    if (result)
        _e = MM::BrowserTypes::BrowserError::NO_ERROR;
    else
        _e = MM::BrowserTypes::BrowserError::BAD_PATH;

    _reply(_e);
}

/**
 * ================================================================================
 * @fn : setLocale
 * @brief : set locale value and reload collation rule.
 * @section : Function flow (Pseudo-code or Decision Table)
 *  call setLocale() of BrowserProvider.
 * @param[input] locale : locale string ( "zh_CN", "ko_KR", ...)
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : none
 * ===================================================================================
 */
void BrowserStubImpl::setLocale(const std::shared_ptr<CommonAPI::ClientId> _client,
                                std::string _locale,
                                setLocaleReply_t _reply)
{
    MMLogInfo("");
    mBrowser->setLocale(_locale);
    _reply();
}

/**
 * ================================================================================
 * @fn : getSongCountByDevice
 * @brief : get song count information of each device from LMS database(indexed data).
 * @section : Function flow (Pseudo-code or Decision Table)
 *  call getSongCountInfo of BrowserProvider.
 * @param[out] songCountList : ResultMapList, each ResultMap includes "Device" and "Count"
 * @param[out] _e : result code (NO_ERROR/NO_CONNECTION/BAD_PATH)
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : none
 * ===================================================================================
 */
void BrowserStubImpl::getSongCountByDevice(const std::shared_ptr<CommonAPI::ClientId> _client,
                                           MM::BrowserStub::getSongCountByDeviceReply_t _reply)
{
    MMLogInfo("");
    MM::BrowserTypes::BrowserError _e;
    MM::MediaTypes::ResultMapList _songCountList;

    bool result = mBrowser->getSongCountByDevice(_songCountList);
    if (result )
    {
        MMLogInfo("success");
        _e = MM::BrowserTypes::BrowserError::NO_ERROR;
    }
    else
    {
        _e = MM::BrowserTypes::BrowserError::BAD_PATH;
    }
    _reply(_songCountList, _e);
}

/**
 * ================================================================================
 * @fn : reopenDatabase
 * @brief :  close and reopen LMS database.
 * @section : Function flow (Pseudo-code or Decision Table)
 * call reopenDatabase() of BrowserProvider.
 * @param[out] result : result of reopenDatabase() call. true if success.
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : none
 * ===================================================================================
 */
void BrowserStubImpl::reopenDatabase(const std::shared_ptr<CommonAPI::ClientId> _client,
                                     reopenDatabaseReply_t _reply)
{
    MMLogInfo("");
    bool result = mBrowser->reopenDatabase();
    _reply(result);
}

/**
 * ================================================================================
 * @fn : requestAudioInfo
 * @brief :  request audio metadata information to ExtractorInterface.
 * @section : Function flow (Pseudo-code or Decision Table)
 * call requestCoverArts of BrowserProvider.
 * @param[in] _path : audio file path.
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : none
 * ===================================================================================
 */
void BrowserStubImpl::requestAudioInfo(const std::shared_ptr<CommonAPI::ClientId> _client,
                                       string _path, requestAudioInfoReply_t _reply)
{
    MMLogInfo("");
    mBrowser->requestAudioInfo(_path);
    _reply();
}

} // namespace mm
} // names

