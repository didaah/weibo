<?php
// $Id$

/**
 * @file oauth2 版本的 api 文件
 */

/**
 * 获取 http 请求所需的参数
 * @param array $params
 */
function weibo_get_request(array $params) {
  $request = new stdClass;
  
  if (isset($params['http_method'])) {
    $request->http_method = $params['http_method'];
    unset($params['http_method']);
  } else {
    $request->http_method = 'POST';
  }
  
  if (isset($params['http_url'])) {
    $request->http_url = $params['http_url'];
    unset($params['http_url']);
  } else {
    return false;
  }

  // 有分界符，则表示需要上传文件
  if (!empty($params['boundary'])) {
    $boundary = $params['boundary'];
    $_values = $params; // 保存一份原始数据
    unset($params['boundary']);
  } else {
    $boundary = false;
  }

  $keys = weibo_urlencode_rfc3986(array_keys($params));
  $values = weibo_urlencode_rfc3986(array_values($params));
  $params = array_combine($keys, $values);
  
  uksort($params, 'strcmp');

  // 提交数据
  $urls = array();

  // 文件数据
  $files = array();

  /**
   * 新浪微博接口未完全遵守 oauth 标准
   * 除文件上传外，所有数据均需要 url 编码
   * 文件上传时，使用 multipart/form-data 模式，不需要 url 编码
   * 签名数据均需要编码
   */
  foreach ($params as $key => $value ) {
    if (is_array($value)) {
      natsort($value);
      foreach ($value as $v2) {
        $urls[$key] = $key . '[]=' . $v2;
      }
    } else if (!in_array($key, array('file', 'pic', 'image'))) {
      $urls[$key] = $key . '=' . $value;
    } else {
      // 文件不参与签名
      $files[$key] = $_values[$key];
    }
  }
  
  $request->field_data = $urls;

  // 文本
  if (empty($boundary)) {
    $request->to_data = implode('&', $urls);
  } else if (!empty($files)) {  // 文件
    
    // 分界符
    $MPboundary = '--'.$boundary;
    $endMPboundary = $MPboundary . '--';

    $request->to_data = '';
    
    // 签名以及签名中的数据
    foreach ($urls as $key => $value) {
      $request->to_data .= $MPboundary . "\r\n";
      $request->to_data .= 'Content-Disposition: form-data; name="'  . $key . "\"\r\n\r\n";
      
      // multipart/form-data 方式提交的数据不需要 urlencode 编码
      $request->to_data .= $_values[$key]. "\r\n";
    }

    // 待上传的文件
    foreach ($files as $key => $value) {
      $request->to_data .= $MPboundary . "\r\n";
      $request->to_data .= 'Content-Disposition: form-data; name="' . $key . '"; filename="' . basename($value) . '"'. "\r\n";
      $request->to_data .= 'Content-Type: '. file_get_mimetype(end(explode('.', $value))) . "\r\n\r\n";
      $request->to_data .= file_get_contents($value) . "\r\n";   
    }

    $request->to_data .=  $endMPboundary;
  } else {
    return false;
  }

  return $request;
}

/**
 * 获取授权令牌，让用户使用此链接跳转到新浪进行授权
 * @param array $values
 * @return string
 */
function weibo_get_login_url(array $values = array()) {

  ini_set('arg_separator.output', '&');

  if (empty($values['client_id'])) {
    $values['client_id'] = var_get_key('weibo', 'oauth_consumer_key');
  }

  if (!empty($_GET['oauth_callback'])) {
    $redirect = $_GET['oauth_callback'];
  } else if (!empty($values['oauth_callback'])) {
    $redirect = $values['oauth_callback'];
    unset($values['oauth_callback']);
  } else if (!empty($_REQUEST['redirect'])) {
    $redirect = $_REQUEST['redirect'];
  } else {
    $redirect = 'user/center/weibo'; 
  }

  $values['redirect_uri'] = url('weibo/validate', array('query' => 'op=validate&redirect=' . urlencode($redirect), 'absolute' => true));

  if (empty($values['response_type'])) {
    $values['response_type'] = 'code';
  }

  $url = 'https://api.weibo.com/oauth2/authorize?' . http_build_query($values);
  
  return $url;
}

/**
 * 获取访问令牌，用户已授权，根据授权令牌获得访问令牌
 * @param array $values
 * @param array|bool
 */
function weibo_get_access_token(array $values = array()) {

  ini_set('arg_separator.output', '&');

  $http_url = 'https://api.weibo.com/oauth2/access_token';

  if (empty($values['client_id'])) {
    $values['client_id'] = var_get_key('weibo', 'oauth_consumer_key');
  }

  if (empty($values['client_secret'])) {
    $values['client_secret'] = var_get_key('weibo', 'oauth_consumer_secret');
  }

  if (empty($values['grant_type'])) {
    $values['grant_type'] = 'authorization_code';
  }

  if (!empty($_REQUEST['code'])) {
    $values['code'] = $_REQUEST['code'];
  }

  if (empty($values['redirect_uri'])) {
    $values['redirect_uri'] = url('user/center/weibo', array('absolute' => true));
  }
  
  $result = dd_http_request($http_url, $values);
  
  if ($result->code == 200 && $result->data) {
    $tok = json_decode($result->data);
    if (!empty($tok->access_token)) {
      return $tok;
    }
  }
}

/**
 * 微博操作类 
 */
class weibo {

  // 用户 uid
  public $uid = 0;

  // 微博帐号 id，对应 user_ext 表
  public $aid = 0;

  // 微博授权信息
  public $token;

  // 错误信息
  public $error = NULL;

  // http request
  public $result = NULL;

  // 授权令牌
  public $access_token = '';

  // 待提交数据
  protected $_values = array(); 

  /**
   * 构造函数 
   * @param int $uid 
   * @param int $aid 
   * @param int $token 
   * @access public
   * @return bool
   */
  public function __construct($uid = 0, $aid = 0, $token = 0) {
    if (!empty($token) && !empty($token->access_token)) {
      $this->token = $token;
    } else {
      if (!empty($aid)) {
        $info = user_ext_rows_load($aid);
      } else if (!empty($uid)) {
        $info = weibo_get_user_default($uid);
      } else {
        return false; 
      }
      
      if (!empty($info)) {
        $this->token = $info->data['token'];
        $this->uid = $info->uid; // 此 uid 为 dida 系统中的 uid，weibo uid ，通过 $this->token->uid 获得
        $this->aid = $info->aid;
      }
    }

    $this->access_token = $this->token->access_token;

    return true;
  }
   
  /**
   * 执行一个 http 请求
   */
  public function http_request($http_url, array $values = array(), array $headers = array(), $method = 'POST') {
    if (!isset($headers['User-Agent'])) {
      $headers['User-Agent'] = 'Sae T OAuth v0.2.0-beta2';
    }
    
    if (!isset($headers['Content-Type'])) {
      $headers['Content-Type'] = 'application/x-www-form-urlencoded; charset=utf-8';
    }

    if (!empty($this->access_token)) {
      $values['access_token'] = $this->access_token;
    }

    $values['http_url'] = $http_url;
    $values['http_method'] = $method;
    
    if (!$request = weibo_get_request($values)) {
      return false;
    }

    // @TODO 新浪 api 针对单个 ip 有严格的访问限制，可自定义一个函数用来分发请求。暂时的处理办法
    if (!function_exists('__weibo_extend_http_request')) {
      $this->result = dd_http_request($request->http_url, $request->to_data, $method, $headers);
    } else {
      $this->result = __weibo_extend_http_request($request->http_url, $request->to_data, $method, $headers, $host);
    }

    if ($this->result->code == 200) {
      $data = json_decode($this->result->data);
      if (is_object($data) && !empty($data->error_code)) {
        $this->error = $data;
        $this->error->client_host = $host;
      } else {
        return $data;
      }
    } else if ($json = json_decode($this->result->data)) {
      $this->error = $json;
      if ($json->error_code == 21327 || $json->error_code == 21332) {
        $this->error->is_token = true;
        dd_set_message(t('weibo', '授权已过期，请通过新浪微博重新授权，' . l('点击这儿', 'weibo/validate')), 'error');
      } else {
        dd_set_message($json->error_code . ':' . $json->error, 'error');
      }
    }
  }
   
  /**
   * 根据当前 token 获取用户基本信息
   * @link http://open.weibo.com/wiki/Account/verify_credentials
   * @access public
   * @return object
   * @TODO 与 1.0 接口同步，所以提供此方法
   */
  public function get_user_info() {
    if (!empty($this->token->uid)) {
      return $this->get_user_show(array('uid' => $this->token->uid));
    }
  }
    
  /**
   * 根据用户名、id 获取用户基本信息
   * @link http://open.weibo.com/wiki/Users/show
   * @param array $array
   *  uid、screen_name 必有其一
   * @access public
   * @return object
   */
  public function get_user_show(array $array) {
    return $this->http_request('https://api.weibo.com/2/users/show.json', $array, array(), 'GET');
  }


  /**
   * 发布一条微博 
   * @param string $text 
   *  微博内容
   * @param string $url
   *  附加到微博末尾的 url
   * @param string $filepath
   *  图片路径，发布为一条图片微博
   * @access public
   * @return object
   */
  public function set_tweet($text, $url = NULL, $filepath = NULL, $annotations = '') {
    // url 长度大于 20，生成短链接
    if (strlen($url) > 20) {
      // 使用微博开放平台的 id
      if ($data = file_get_contents('https://api.weibo.com/2/short_url/shorten.json?url_long=' . urlencode($url) . '&source=543368493')) {
        if ($data = json_decode($data)) {
          if (!empty($data->urls) && $data->urls[0]->result) {
            $url = $data->urls[0]->url_short;
          }
        }
      }
    }
    
    if (!empty($filepath)) {
      return $this->set_upload($filepath, $text, $url);
    }

    $values = array(
      'status' => weibo_text_strlen($text, $url)
    );

    if (!empty($annotations)) {
      $values['annotations'] = $annotations;
    }
     
    if ($data = $this->http_request('https://api.weibo.com/2/statuses/update.json', $values)) {
      $data->user->statuses_count += 1;
      weibo_set_user_data($this->aid, array('info' => $data->user));
      return $data;
    }
  }

  /**
   * 发布一条图片微博 
   * @param string $filepath 
   *  图片地址
   * @param string $text 
   *  微博内容
   * @access public
   * @return object
   */
  public function set_upload($filepath, $text = NULL, $url = NULL) {
    
    $values = array();

    if ($text = weibo_text_strlen($text, $url)) {
      $values['status'] = $text;
    }
    
    $values['pic'] = $filepath;
    
    $values['source'] = var_get_key('weibo', 'oauth_consumer_key');
    
    $values['boundary'] = uniqid('------------------');

    if ($data = $this->http_request(
      'https://upload.api.weibo.com/2/statuses/upload.json',
      $values,
      array('Content-Type' => 'multipart/form-data; boundary=' . $values['boundary'])
    )) {
      $data->user->statuses_count += 1;
      weibo_set_user_data($this->aid, array('info' => $data->user));
      return $data;
    }
  }

  /**
   * 获取用户的微博列表，不指定则读取当前授权用户
   * @link http://open.weibo.com/wiki/Statuses/user_timeline
   * @link http://open.weibo.com/wiki/2/Statuses/user_timeline
   * @param int $uid 
   *  微博 uid，指定则按 uid 读取
   * @param int $screen_name 
   *  微博显示用户名，指定则按用户名读取
   * @param array $array
   *  其它数据
   * @access public
   * @return array
   */
  public function get_timeline($uid = 0, $screen_name = 0, array $array = array()) {
    $values = $array;
    
    if (!empty($screen_name)) $values['screen_name'] = $screen_name;

    if (!empty($uid)) $values['uid'] = $uid;
    
    return $this->http_request('https://api.weibo.com/2/statuses/user_timeline.json', $values, array(), 'GET');
  }

  /**
   * 获取一组微博的评论和转发数 
   * @param string $ids 
   * @access public
   * @return array
   */
  public function get_counts($ids) {
    return $this->http_request('https://api.weibo.com/2/statuses/count.json', array('ids' => $ids), array(), 'GET');
  }

  /**
   * 对微博或回复，进行评论
   * @link http://open.weibo.com/wiki/Statuses/comment 
   * @param string $text
   *  评论内容，不超过 140 字
   * @param int $id 
   *  微博 id
   * @param int $cid 
   *  回复 id
   * @param array $array 
   *  其它参数
   * @access public
   * @return object
   */
  public function set_comment($text, $id, $cid = 0, $array = array('without_mention' => 0, 'comment_ori' => 0)) {
    $values = array('comment' => weibo_text_strlen($text));

    if (!empty($cid)) {
      // 对评论进行回复
      $url = 'https://api.weibo.com/2/comments/reply.json';
      $values['cid'] = $cid;
    } else {
      // 对微博进行回复
      $url = 'https://api.weibo.com/2/comments/create.json';
    }

    $values['id'] = $id;

    return $this->http_request($url, $values);
  }

  /**
   * 删除一条评论
   * @link http://open.weibo.com/wiki/2/comments/destroy
   * @param int $cid 
   *  评论 id
   * @access public
   * @return object
   */
  public function delete_comment($cid) {
    return $this->http_request('https://api.weibo.com/2/comments/destroy.json', array('cid' => $cid));
  }

  /**
   * 根据微博 id 获取微博信息
   * @param int $id 
   *  指定微博 id
   * @access public
   * @return object
   */
  public function get_text($id) {
    return $this->http_request('https://api.weibo.com/2/statuses/show.json', array('id' => $id), array(), 'GET');
  }
  
  /**
   * 获取表情 
   * @link http://open.weibo.com/wiki/2/emotions
   * @param string $type 
   *  表情类别，"face":普通表情，"ani"：魔法表情，"cartoon"：动漫表情 
   * @param string $lang 
   *  语言类别，"cnname"简体，"twname"繁体 
   * @access public
   * @return array
   */
  public function get_emotions($type = 'face', $lang = 'cnname') {
    $values = array();
    $values['type'] = $type;
    $values['language'] = $lang;

    return $this->http_request('https://api.weibo.com/2/emotions.json', $values, array(), 'GET');
  }

  /**
   * 获取当前用户发出的评论
   * @link http://open.weibo.com/wiki/Statuses/comments_by_me
   * @link http://open.weibo.com/wiki/2/comments/by_me
   * @param int $page_size 
   *  页码
   * @param int $page_count 
   *  每页评论数量，最大 200
   * $param array $options
   * @access public
   * @return array
   */
  public function get_comment_by_me($page_size = 1, $page_count = 20, $options = array()) {
    $values = array();
    $values['count'] = $page_count;
    $values['page'] = $page_size;

    $values += $options;

    return $this->http_request('https://api.weibo.com/2/comments/by_me.json', $values, array(), 'GET');
  }

  /**
   * 获取当前用户收到的评论
   * @link http://open.weibo.com/wiki/Statuses/comments_to_me
   * @link http://open.weibo.com/wiki/2/comments/to_me
   * @param int $page_size 
   *  页码
   * @param int $page_count 
   *  每页评论数量，最大 200
   * $param array $options
   * @access public
   * @return array
   */
  public function get_comment_to_me($page_size = 1, $page_count = 20, $options = array()) {
    $values = array();
    $values['count'] = $page_count;
    $values['page'] = $page_size;

    $values += $options;

    return $this->http_request('https://api.weibo.com/2/comments/to_me.json', $values, array(), 'GET');
  }

  /**
   * 根据微博 id 获取评论 
   * @link http://open.weibo.com/wiki/2/comments/show
   * @param int $id 
   *  指定微博 id
   * @param int $page_size 
   *  页码
   * @param int $page_count 
   *  每页评论数量，最大 200
   * @access public
   * @return array
   */
  public function get_comment($id, $page_size = 1, $page_count = 20, $options = array()) {
    $values = array();
    $values['id'] = $id;
    $values['count'] = $page_count;
    $values['page'] = $page_size;

    $values += $options;

    return $this->http_request('https://api.weibo.com/2/comments/show.json', $values, array(), 'GET');
  }

  /**
   * 根据微博 id 获取转发 
   * @link http://open.weibo.com/wiki/2/statuses/repost_timeline
   * @param int $id 
   *  指定微博 id
   * @param int $page_size 
   *  页码
   * @param int $page_count 
   *  每页评论数量，最大 200
   * @access public
   * @return array
   */
  public function get_repost($id, $page_size = 1, $page_count = 20, $options = array()) {
    $values = array();
    $values['id'] = $id;
    $values['count'] = $page_count;
    $values['page'] = $page_size;
    
    $values += $options;

    return $this->http_request('https://api.weibo.com/2/statuses/repost_timeline.json', $values, array(), 'GET');
  }

  public function get_friends($id, $page_size = 0, $page_count = 50) {
    $values = array(
      'uid' => $id,
      'count' => $page_count,
      'cursor' => $page_size
    );

    return $this->http_request('https://api.weibo.com/2/friendships/friends.json', $values, array(), 'GET');
  }

  /**
   * @link http://open.weibo.com/wiki/Statuses/followers
   * @link http://open.weibo.com/wiki/2/friendships/followers
   * 根据用户 uid 获取粉丝
   * @param int $uid 
   *  用户 uid
   * @param int $page_size 
   *  页码，第一页为 -1
   * @param int $page_count 
   *  每页数量，最大 200
   * @access public
   * @return array
   */
  public function get_follower($id, $page_size = 0, $page_count = 50) {
    $values = array();
    $values['uid'] = $id;
    $values['count'] = $page_count;
    $values['cursor'] = $page_size;
    
    return $this->http_request('https://api.weibo.com/2/friendships/followers.json', $values, array(), 'GET');
  }

  /**
   * @link http://open.weibo.com/wiki/2/friendships/followers/ids
   * 根据用户 uid 获取粉丝 id
   * @param int $uid 
   *  用户 uid
   * @param int $page_size 
   *  页码，第一页为 -1
   * @param int $page_count 
   *  每页数量，最大 200
   * @access public
   * @return array
   */
  public function get_follower_ids($id, $page_size = 0, $page_count = 50) {
    $values = array();
    $values['uid'] = $id;
    $values['count'] = $page_count;
    $values['cursor'] = $page_size;
    
    return $this->http_request('https://api.weibo.com/2/friendships/followers/ids.json', $values, array(), 'GET');
  }

  /**
   * 根据 mid 获取 id 
   * @param array $values 
   * @access public
   * @return array
   */
  public function get_queryid($values) {
    return $this->http_request('https://api.weibo.com/2/statuses/queryid.json', $values, array(), 'GET');
  }
    
  /**
   * 获取热门话题 
   * @link http://open.weibo.com/wiki/Trends/daily
   * @link http://open.weibo.com/wiki/Trends/weekly
   * @param bool $weekly 
   *  获取当周，默认为当天
   * @param int $base_app 
   *  是否限制在本应用之内
   * @access public
   * @return array
   */
  public function get_hot_trends($weekly = false, $base_app = 0) {

    $values['base_app'] = array('base_app' => $base_app);

    if (!$weekly) {
      $url = 'https://api.weibo.com/2/trends/daily.json';
    } else {
      $url = 'https://api.weibo.com/2/trends/weekly.json';
    }

    return $this->http_request($url, $values, array(), 'GET');
  }
 
  /**
   * 获取访问次数剩余量
   * @link http://open.weibo.com/wiki/2/account/rate_limit_status
   * @access public
   * @return array
   */
  public function get_limit_status() {
    return $this->http_request('https://api.weibo.com/2/account/rate_limit_status.json', array(), array(), 'GET');
  }

  /**
   * 批量获取用户的关注、粉丝、微博数量
   * @link http://open.weibo.com/wiki/2/users/counts 
   * @param string $uids 
   *  用户 uid，最多 100 个，以逗号分隔
   * @access public
   * @return object
   */
  public function get_user_counts($uids) {
    return $this->http_request('https://api.weibo.com/2/users/counts.json', array('uids' => $uids), array(), 'GET'); 
  } 

  /**
   * 获取最新公共微博
   * @link http://open.weibo.com/wiki/2/statuses/public_timeline
   * @param string $count 
   *  获取数量，默认 200，最大 200
   * @access public
   * @return object
   */
  public function get_public_timeline($count = 200) {
    return $this->http_request('https://api.weibo.com/2/statuses/public_timeline.json', array('count' => $count), array(), 'GET'); 
  } 

  /**
   * 获取当前用户的收藏 
   * @access public
   * @return object
   */
  public function get_favorites($page = 1, $count = 50) {
    return $this->http_request('https://api.weibo.com/2/favorites.json', array('count' => $count, 'page' => $page), array(), 'GET'); 
  }

  /**
   * 验证 token 是否有效
   * @access public
   * @return bool
   */
  public function is_token() {
    return $this->http_request('https://api.weibo.com/2/account/get_uid.json', array(), array(), 'GET'); 
  }

  /**
   * 执行一个 api 请求
   * @param string $url 
   *  api 地址
   * @param array $values 
   *  传递的参数，传递给此接口的参数，将不做任何处理
   * @param string $method
   *  方法，默认为 GET
   * @access public
   * @return object
   */
  public function execute($url, $values = array(), $method = 'GET') {
    return $this->http_request($url, $values, array(), $method); 
  }
}
