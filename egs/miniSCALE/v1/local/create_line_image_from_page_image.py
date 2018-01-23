#!/usr/bin/env python3

import argparse
import os
import xml.dom.minidom as minidom
from PIL import Image

parser = argparse.ArgumentParser(description="""Creates line images from page image.""")
parser.add_argument('database_path', type=str,
                    help='Path to the downloaded (and extracted) mdacat data')
parser.add_argument('data_splits', type=str,
                    help='Path to file that contains the train/test/dev split information')
parser.add_argument('--width_buffer', type=int, default=0,
                    help='width buffer across annotate character')
parser.add_argument('--height_buffer', type=int, default=0,
                    help='height buffer across annotate character')
parser.add_argument('--char_width_buffer', type=int, default=10,
                    help='white space between two characters')
parser.add_argument('--char_height_buffer', type=int, default=20,
                    help='starting location from the top of the line')
args = parser.parse_args()

def merge_characters_into_line_image(region_list, offset_list):
    # get image width and height
    image_width = 0
    image_height = -1
    for x in region_list:
        (width, height) = x.size
        image_width += width + char_width_buffer
        image_height = max(image_height, height)
    image_height += char_height_buffer*2

    stitched_image = Image.new('RGB', (image_width, image_height), "white")
    width_offset = 0
    for i, x in enumerate(region_list):
        height_offset = offset_list[i] + int(char_height_buffer)
        stitched_image.paste(im=x, box=(width_offset, height_offset))
        (width, height) = x.size
        width_offset = width_offset + width + char_width_buffer

    return stitched_image


def get_line_images_from_page_image(image_file_name, gedi_file_path):
    height_offset_list = list()
    region_list = list()
    first_image_top_loc = -1
    previous_line_id = "-1"
    start = "true"

    im = Image.open(image_file_name)
    doc = minidom.parse(gedi_file_path)
    DL_ZONE = doc.getElementsByTagName('DL_ZONE')
    for node in DL_ZONE:
        line_id = node.getAttribute('lineID')
        if line_id == "":
            continue

        col = int(node.getAttribute('col'))
        row = int(node.getAttribute('row'))
        if start == "true":
            first_image_top_loc = row
            previous_line_id = line_id
            region_list = list()
            height_offset_list = list()

        if previous_line_id != line_id:
            image = merge_characters_into_line_image(region_list, height_offset_list)
            set_line_image_data(image, previous_line_id, image_file_name)

            first_image_top_loc = row
            previous_line_id = line_id
            region_list = list()
            height_offset_list = list()

        height_offset = row - first_image_top_loc
        height_offset_list.append(height_offset)

        # get character dimensions
        col -= height_buffer
        row -= width_buffer
        width = int(node.getAttribute('width'))
        height = int(node.getAttribute('height'))
        col_down = col + width + height_buffer
        row_right = row + height + width_buffer
        box = (col, row, col_down, row_right)

        # crop characters
        region = im.crop(box)
        region_list.append(region)
        start = "false"
    image = merge_characters_into_line_image(region_list, height_offset_list)
    set_line_image_data(image, previous_line_id, image_file_name)


def set_line_image_data(image, line_id, image_file_name):
    base_name = os.path.splitext(os.path.basename(image_file_name))[0]
    image_file_name_wo_tif, b = image_file_name.split('.tif')
    line_id = '_' + line_id.zfill(3)
    line_image_file_name = base_name + line_id + '.tif'
    imgray = image.convert('L')
    image_path=os.path.join(data_path, 'lines', line_image_file_name)
    imgray.save(image_path)

def remove_corrupt_xml_files(gedi_file_path):
    doc = minidom.parse(gedi_file_path)
    line_id_list = list()
    unique_lineid = list()
    DL_ZONE = doc.getElementsByTagName('DL_ZONE')
    for node in DL_ZONE:
        line_id = node.getAttribute('lineID')
        if line_id == "":
            continue
        line_id_list.append(int(line_id))
        unique_lineid = list(set(line_id_list))
        unique_lineid.sort()
    
    #check if the lineID is empty
    if len(line_id_list) == 0:
        print("Error: lineID missing line_id: {0}".format(line_id))
        return False

    # check if list contain consequtive entries
    if len(sorted(unique_lineid)) != len(range(min(unique_lineid), max(unique_lineid)+1)):
        print("Error: lineID list does not contain all consequtive entries: {0}".format(line_id))
        return False

    # process the file
    return True

### main ###

data_path = args.database_path
height_buffer = int(args.height_buffer)
width_buffer = int(args.width_buffer)
char_width_buffer = int(args.char_width_buffer)
char_height_buffer = int(args.char_height_buffer)

with open(args.data_splits) as f:
    prev_base_name = ''
    for line in f:
        base_name = os.path.splitext(os.path.splitext(line.split(' ')[0])[0])[0]
        if prev_base_name != base_name:
            prev_base_name = base_name
            gedi_file_path = os.path.join(args.database_path, 'gedi', base_name + '.gedi.xml')
            image_file_path = os.path.join(args.database_path, 'images', base_name + '.tif')
            if remove_corrupt_xml_files(gedi_file_path):
                get_line_images_from_page_image(image_file_path, gedi_file_path)
