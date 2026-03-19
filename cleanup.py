import re

with open('d:/Ai/Print-311-1/main/web_server.c', 'r', encoding='utf-8') as f:
    content = f.read()

# Remove all OTA related code blocks
# Find the pattern from "#define OTA_BUFFER_SIZE" to the last "}" before "web_server_get_embedded_resource"

# Find OTA_BUFFER_SIZE and everything after it until web_server_get_embedded_resource
pattern = r'#define OTA_BUFFER_SIZE.*?web_server_get_embedded_resource'
content = re.sub(pattern, 'web_server_get_embedded_resource', content, flags=re.DOTALL)

# Also need to remove the old ota_url_handler registration
pattern2 = r'httpd_uri_t ota_uri = \{.*?\};\s*httpd_register_uri_handler\(g_server, &ota_uri\);\s*'
content = re.sub(pattern2, '', content, flags=re.DOTALL)

with open('d:/Ai/Print-311-1/main/web_server.c', 'w', encoding='utf-8') as f:
    f.write(content)

print("Done")
