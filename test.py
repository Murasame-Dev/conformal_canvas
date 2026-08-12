import requests
from PIL import Image
from io import BytesIO

img = Image.open("test.png")

img_byte_arr = BytesIO()
img.save(img_byte_arr, format='PNG')
img_byte_arr = img_byte_arr.getvalue()

res = requests.post(url="http://10.88.0.60:7854/handle_escher_image",
                    headers={"Content-Type": "image/png"}, data=img_byte_arr)
print(res.status_code)
if res.status_code != 200:
    print(res.text)
test_res = res.content
img = Image.open(BytesIO(test_res))
img.save("test_out.png")

res = requests.post(url="http://10.88.0.60:7854/handle_conformal_image", headers={"Content-Type": "image/png"},
                    data=img_byte_arr, params={"func": "(z*100^2)^(1/2)"})
print(res.status_code)
if res.status_code != 200:
    print(res.text)
test_res = res.content
img = Image.open(BytesIO(test_res))
img.save("test_out_conformal.png")
