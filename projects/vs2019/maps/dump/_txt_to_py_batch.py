import os
import json
import re

txt_files = [f for f in os.listdir() if f.endswith(".txt")]

for txt_file in txt_files:
    base_name = os.path.splitext(txt_file)[0]
    class_name = base_name

    entries = []

    with open(txt_file, "r") as f:
        for idx, line in enumerate(f, start=1):
            original_line = line.strip().rstrip(',')

            if not original_line:
                continue

            # Handle missing UUIDs (e.g., "uuid": })
            fixed_line = re.sub(r'"uuid"\s*:\s*}', '"uuid": null}', original_line)
            # Handle missing UUIDs (e.g., "uuid": ,)
            fixed_line = re.sub(r'"uuid"\s*:\s*,', '"uuid": null,', fixed_line)

            try:
                entry = json.loads(fixed_line)
                if entry.get("uuid") is None:
                    print(f"Missing UUID in {txt_file} (line {idx}), setting uuid = 0")
                entries.append(entry)
            except json.JSONDecodeError as e:
                print(f"❌ Skipping malformed line in {txt_file} (line {idx}): {original_line}")
                continue

    location_defs_str = ",\n".join([
    "        {{\n            \"id\": {id},\n            \"name\": \"{name}\",\n            \"classname\": \"{classname}\",\n            \"uuid\": {uuid},\n            \"targetname\": \"{targetname}\"\n        }}".format(
        id=entry['id'],
        name=entry['name'],
        classname=entry['classname'],
        uuid=entry['uuid'] if entry['uuid'] is not None else "0",
        targetname=entry.get('targetname', "")
    )
    for entry in entries
    ])

    py_content = f'''from BaseClasses import Region

from ..base_classes import HLLevel


class {class_name}(HLLevel):
    name = "{base_name}"
    mapfile = "{base_name}"
    location_defs = [
{location_defs_str}
    ]

    def main_region(self) -> Region:
        r = self.rules

        ret = self.region(
            self.name,
            [],
        )
        return ret
'''

    py_file = f"{base_name}.py"
    with open(py_file, "w") as out:
        out.write(py_content)

    print(f"{py_file} generated.")