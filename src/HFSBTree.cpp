#include "HFSBTree.h"
#include <stdexcept>
#include "be.h"
#include "hfsplus.h"
#include "unichar.h"
#include <iostream>
#include <cstring>
#include "HFSBTreeNode.h"

HFSBTree::HFSBTree(HFSFork* fork)
: m_fork(fork)
{
	BTNodeDescriptor desc0;
	
	std::cout << "Tree size: " << fork->size() << std::endl;
	m_tree = new char[fork->size()];
	fork->read(m_tree, fork->size(), 0);
	
	memcpy(&desc0, m_tree, sizeof(desc0));
	
	if (desc0.kind != NodeKind::kBTHeaderNode)
		throw std::runtime_error("Wrong kind of BTree header");
	
	memcpy(&m_header, m_tree + sizeof(desc0), sizeof(m_header));
	
	std::cout << "leaf records: " << be(m_header.leafRecords) << std::endl;
	std::cout << "node size: " << be(m_header.nodeSize) << std::endl;
	
	if (m_header.rootNode)
	{
		walkTree(be(m_header.rootNode));
	}
}

HFSBTreeNode HFSBTree::findLeafNode(const Key* indexKey, KeyComparator comp)
{
	return traverseTree(be(m_header.rootNode), indexKey, comp);
}

std::vector<HFSBTreeNode> HFSBTree::findLeafNodes(const Key* indexKey, KeyComparator comp)
{
	std::vector<HFSBTreeNode> rv;
	HFSBTreeNode current = findLeafNode(indexKey, comp);

	if (current.isInvalid())
		return rv;

	while (current.forwardLink() != 0)
	{
		Key* key;

		current = HFSBTreeNode(m_tree, current.forwardLink(), current.nodeSize());
		
		key = current.getKey<Key>(); // TODO: or the key of the first record?

		if (!comp(key, indexKey))
			break;

		rv.push_back(current);
	}

	return rv;
}

HFSBTreeNode HFSBTree::traverseTree(int nodeIndex, const Key* indexKey, KeyComparator comp)
{
	HFSBTreeNode node(m_tree, nodeIndex, be(m_header.nodeSize));

	switch (node.kind())
	{
		case NodeKind::kBTIndexNode:
		{
			int position = node.recordCount() / 2;
			int distance = position;
			uint32_t* childIndex;
			Key* key;

			// binary search for the next node
			while (distance > 1)
			{
				key = node.getRecordKey<Key>(position);

				distance /= 2;

				if (comp(key, indexKey))
					position -= distance;
				else
					position += distance;
			}

			// recurse down
			childIndex = node.getRecordData<uint32_t>(position);
			return traverseTree(be(*childIndex), indexKey, comp);
		}
		case NodeKind::kBTLeafNode:
		{
			return node;
		}
		case NodeKind::kBTHeaderNode:
		case NodeKind::kBTMapNode:
			break;
	}

	return HFSBTreeNode();
}

void HFSBTree::walkTree(int nodeIndex)
{
	BTNodeDescriptor* desc;
	uint32_t offset = be(m_header.nodeSize)*nodeIndex;
	uint16_t* firstRecordOffset;
	
	desc = reinterpret_cast<BTNodeDescriptor*>(m_tree + be(m_header.nodeSize)*nodeIndex);
	firstRecordOffset = reinterpret_cast<uint16_t*>(m_tree + be(m_header.nodeSize)*(nodeIndex+1) - sizeof(uint16_t));
	
	switch (desc->kind)
	{
		case NodeKind::kBTLeafNode:
		{
			HFSPlusCatalogKey* key = reinterpret_cast<HFSPlusCatalogKey*>(((char*) desc) + sizeof(BTNodeDescriptor));
			std::cout << "LeafNode " << nodeIndex << " is a leaf node: " << UnicharToString(key->nodeName) << std::endl;
			std::cout << "LeafSibling: " << be(desc->fLink) << std::endl;
			std::cout << "LeafRecords: " << be(desc->numRecords) << std::endl;
			
			for (long i = 0; i < be(desc->numRecords); i++)
			{
				uint16_t recordOffset = be(*(firstRecordOffset-i));
				HFSPlusCatalogKey* recordKey = reinterpret_cast<HFSPlusCatalogKey*>(((char*) desc) + recordOffset);
				HFSPlusCatalogFile* record;
				RecordType recType;
				
				std::cout << "LeafRecordKey: " << UnicharToString(recordKey->nodeName) << " - parent: " << be(recordKey->parentID) << std::endl;
				record = reinterpret_cast<HFSPlusCatalogFile*>(((char*) recordKey) + be(recordKey->keyLength) + sizeof(recordKey->keyLength));
				recType = RecordType(be(uint16_t(record->recordType)));
				
				switch (recType)
				{
					case RecordType::kHFSPlusFolderRecord:
					{
						HFSPlusCatalogFolder* file = (HFSPlusCatalogFolder*) record;
						std::cout << "\tFolder: ID " << be(file->folderID) << std::endl;
						break;
					}
					case RecordType::kHFSPlusFileRecord:
					{
						HFSPlusCatalogFile* file = (HFSPlusCatalogFile*) record;
						std::cout << "\tFile: ID " << be(file->fileID) << std::endl;
						break;
					}
					case RecordType::kHFSPlusFolderThreadRecord:
					{
						HFSPlusCatalogThread* thread = (HFSPlusCatalogThread*) record;
						std::cout << "\tA folder named " << UnicharToString(thread->nodeName) << " with CNID " << be(recordKey->parentID) << " has parent CNID " << be(thread->parentID) << std::endl;
						break;
					}
					case RecordType::kHFSPlusFileThreadRecord:
					{
						HFSPlusCatalogThread* thread = (HFSPlusCatalogThread*) record;
						std::cout << "\tA file named " << UnicharToString(thread->nodeName) << " with CNID " << be(recordKey->parentID) << " has parent CNID " << be(thread->parentID) << std::endl;
						break;
					}
					default:
					{
						std::cout << "\tunknown record type: " << be(uint16_t(record->recordType)) << std::endl;
					}
				}
			}
			break;
		}
		case NodeKind::kBTIndexNode:
		{
			std::cout << "Node " << nodeIndex << " is an index node with " << be(desc->numRecords) << " records\n";
			//std::cout << "Sibling: " << be(desc->fLink) << std::endl;
			
			for (long i = 0; i < be(desc->numRecords); i++)
			{
				uint16_t recordOffset = be(*(firstRecordOffset-i));
				HFSPlusCatalogKey* record = reinterpret_cast<HFSPlusCatalogKey*>(((char*) desc) + recordOffset);
				uint16_t keyLen = be(record->keyLength); // TODO:  kBTVariableIndexKeysMask
				uint32_t childNodeIndex;
				
				std::cout << "Record " << i << ", key len:" << keyLen << std::endl;
				std::cout << "Index key " << be(record->parentID) << std::endl;
				
				childNodeIndex = be(*(uint32_t*) (((char*)record)+2+keyLen) );
				std::cout << "Child node index: " << childNodeIndex << std::endl;
				walkTree(childNodeIndex);
			}
			break;
		}
		case NodeKind::kBTHeaderNode:
			std::cout << "Node " << nodeIndex << " is a header node\n";
			break;
		case NodeKind::kBTMapNode:
			std::cout << "Node " << nodeIndex << " is a map node\n";
			break;
	}
}

HFSBTree::~HFSBTree()
{
	delete [] m_tree;
	delete m_fork;
}


